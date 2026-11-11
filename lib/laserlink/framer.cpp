#include "framer.h"
#include <string.h>

bool TxFramer::load(const uint8_t* bytes, size_t n)
{
    if (busy_ || n == 0 || n > MAX_PACKET) {
        return false;
    }
    memcpy(buf_, bytes, n);
    n_ = n;
    byteIdx_ = 0;
    bitIdx_ = 0;
    tickIdx_ = 0;
    busy_ = true;
    return true;
}

bool TxFramer::tick()
{
    if (!busy_) {
        return false;
    }

    bool level;
    if (bitIdx_ == 0) {
        level = true;                               // start bit
    } else if (bitIdx_ <= 8) {
        level = (buf_[byteIdx_] >> (8 - bitIdx_)) & 1; // MSB first
    } else {
        level = false;                              // stop bit
    }

    if (++tickIdx_ == TICKS_PER_BIT) {
        tickIdx_ = 0;
        if (++bitIdx_ == BITS_PER_FRAME) {
            bitIdx_ = 0;
            if (++byteIdx_ == n_) {
                busy_ = false;
            }
        }
    }
    return level;
}

bool RxDeframer::tick(bool level, uint16_t adc, RxByte& out)
{
    switch (state_) {
    case WAIT_IDLE:
        // After power-up or a framing error, wait for the line to go dark first.
        if (!level) {
            state_ = IDLE;
        }
        return false;

    case IDLE:
        if (level) {
            // First tick of a start bit.
            state_ = RECEIVING;
            count_ = 0;
            bit_ = 0;
            value_ = 0;
        }
        return false;

    case RECEIVING: {
        count_++;
        // Sample in the middle of each bit, counted from the start edge.
        if (count_ != TICKS_PER_BIT / 2 + bit_ * TICKS_PER_BIT) {
            return false;
        }
        adc_[bit_] = adc;

        if (bit_ == 0) {
            if (!level) {
                state_ = IDLE;      // glitch, not a real start bit
                return false;
            }
        } else if (bit_ <= 8) {
            value_ = (uint8_t)((value_ << 1) | (level ? 1 : 0));
        } else {
            out.value = value_;
            out.framingOk = !level;
            memcpy(out.adc, adc_, sizeof(adc_));
            state_ = level ? WAIT_IDLE : IDLE;
            return true;
        }
        bit_++;
        return false;
    }
    }
    return false;
}
