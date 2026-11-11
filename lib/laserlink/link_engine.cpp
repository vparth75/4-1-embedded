#include "link_engine.h"
#include <string.h>

bool LinkEngine::sendPayload(const uint8_t* p, size_t n)
{
    if (tx_.busy() || n > MAX_PAYLOAD) {
        return false;
    }
    uint8_t wire[MAX_PACKET];
    size_t len = buildPacket(p, n, wire);
    return tx_.load(wire, len);
}

bool LinkEngine::tick(uint16_t adc, uint32_t nowMs)
{
    // Sensor reads low when the laser light hits it.
    bool level = adc < threshold_;

    parser_.tick(nowMs);

    RxByte b;
    if (rx_.tick(level, adc, b)) {
        if (parser_.idle()) {
            // Possible start of a new packet: begin a fresh capture.
            work_.adcCount = 0;
            framingBad_ = false;
        }
        if (work_.adcCount + BITS_PER_FRAME <= MAX_PACKET * BITS_PER_FRAME) {
            memcpy(work_.adc + work_.adcCount, b.adc, sizeof(b.adc));
            work_.adcCount += BITS_PER_FRAME;
        }
        framingBad_ |= !b.framingOk;

        if (parser_.feed(b.value, nowMs) == PacketParser::DONE) {
            if (count_ < QUEUE) {
                RxPacket& slot = queue_[(head_ + count_) % QUEUE];
                slot = work_;
                slot.len = parser_.length();
                memcpy(slot.payload, parser_.payload(), slot.len);
                slot.ok = parser_.ok() && !framingBad_;
                count_++;
            }
            work_.adcCount = 0;
            framingBad_ = false;
        }
    }

    return tx_.tick();
}

bool LinkEngine::takeReceived(RxPacket& out)
{
    if (count_ == 0) {
        return false;
    }
    out = queue_[head_];
    head_ = (head_ + 1) % QUEUE;
    count_--;
    return true;
}
