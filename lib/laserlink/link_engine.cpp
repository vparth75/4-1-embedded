#include "link_engine.h"
#include <string.h>

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define LL_LOCK() portENTER_CRITICAL(&s_mux)
#define LL_UNLOCK() portEXIT_CRITICAL(&s_mux)
#else
#define LL_LOCK()
#define LL_UNLOCK()
#endif

void LinkEngine::setThreshold(uint16_t t)
{
    LL_LOCK();
    threshold_ = t;
    LL_UNLOCK();
}

bool LinkEngine::sendPayload(const uint8_t* p, size_t n)
{
    if (n > MAX_PAYLOAD) {
        return false;
    }
    uint8_t wire[MAX_PACKET];
    size_t len = buildPacket(p, n, wire);
    LL_LOCK();
    bool ok = tx_.load(wire, len);
    LL_UNLOCK();
    return ok;
}

bool LinkEngine::tick(uint16_t adc, uint32_t nowMs)
{
    LL_LOCK();
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

    bool out = tx_.tick();
    LL_UNLOCK();
    return out;
}

bool LinkEngine::takeReceived(RxPacket& out)
{
    LL_LOCK();
    bool have = count_ > 0;
    if (have) {
        out = queue_[head_];
        head_ = (head_ + 1) % QUEUE;
        count_--;
    }
    LL_UNLOCK();
    return have;
}
