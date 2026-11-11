#pragma once
#include <stdint.h>
#include <stddef.h>
#include "packet.h"

// One bit lasts TICKS_PER_BIT ticks. A frame is start(1) + 8 data bits (MSB first) + stop(0).
constexpr int TICKS_PER_BIT = 8;
constexpr int BITS_PER_FRAME = 10;

class TxFramer {
public:
    // Queue bytes to send. False if still sending or more than MAX_PACKET bytes.
    bool load(const uint8_t* bytes, size_t n);
    bool busy() const { return busy_; }
    // Laser level for this tick (true = ON). Off while idle.
    bool tick();

private:
    uint8_t buf_[MAX_PACKET] = {};
    size_t n_ = 0;
    size_t byteIdx_ = 0;
    int bitIdx_ = 0;   // 0 = start, 1..8 = data, 9 = stop
    int tickIdx_ = 0;
    bool busy_ = false;
};

struct RxByte {
    uint8_t value;
    bool framingOk;                    // stop bit was 0
    uint16_t adc[BITS_PER_FRAME];      // reading used for each bit's decision
};

class RxDeframer {
public:
    // Feed one sample. Returns true when a full frame was decoded into `out`.
    bool tick(bool level, uint16_t adc, RxByte& out);

private:
    enum State { WAIT_IDLE, IDLE, RECEIVING };
    State state_ = WAIT_IDLE;
    int count_ = 0;     // ticks since the start edge
    int bit_ = 0;       // next bit to sample (0 = start)
    uint8_t value_ = 0;
    uint16_t adc_[BITS_PER_FRAME] = {};
};
