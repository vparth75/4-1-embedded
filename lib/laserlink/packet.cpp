#include "packet.h"
#include "crc8.h"
#include <string.h>

size_t buildPacket(const uint8_t* payload, size_t len, uint8_t* out)
{
    if (len > MAX_PAYLOAD) {
        return 0;
    }
    out[0] = SYNC_BYTE;
    out[1] = (uint8_t)len;
    if (len > 0) {
        memcpy(out + 2, payload, len);
    }
    out[2 + len] = crc8(out + 1, len + 1);
    return len + 3;
}

void PacketParser::reset()
{
    state_ = WAIT_SYNC;
    got_ = 0;
}

void PacketParser::tick(uint32_t nowMs)
{
    if (state_ != WAIT_SYNC && (uint32_t)(nowMs - lastMs_) > PACKET_TIMEOUT_MS) {
        reset();
    }
}

PacketParser::Result PacketParser::feed(uint8_t b, uint32_t nowMs)
{
    tick(nowMs);
    lastMs_ = nowMs;

    switch (state_) {
    case WAIT_SYNC:
        if (b == SYNC_BYTE) {
            state_ = WAIT_LEN;
        }
        return NONE;

    case WAIT_LEN:
        if (b > MAX_PAYLOAD) {
            reset();
            return NONE;
        }
        len_ = b;
        got_ = 0;
        state_ = (len_ == 0) ? WAIT_CRC : PAYLOAD;
        return NONE;

    case PAYLOAD:
        buf_[got_++] = b;
        if (got_ == len_) {
            state_ = WAIT_CRC;
        }
        return NONE;

    case WAIT_CRC: {
        uint8_t tmp[MAX_PAYLOAD + 1];
        tmp[0] = (uint8_t)len_;
        memcpy(tmp + 1, buf_, len_);
        ok_ = (crc8(tmp, len_ + 1) == b);
        reset();
        return DONE;
    }
    }
    return NONE;
}
