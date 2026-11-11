#pragma once
#include <stdint.h>
#include <stddef.h>
#include "packet.h"
#include "framer.h"

struct RxPacket {
    uint8_t payload[MAX_PAYLOAD];
    size_t len;
    bool ok;                                         // CRC matched and every frame had a good stop bit
    uint16_t adc[MAX_PACKET * BITS_PER_FRAME];       // per-bit readings from SYNC to CRC
    size_t adcCount;
};

// Ties TX framing, RX framing and packet parsing together.
// Call tick() at TICKS_PER_BIT ticks per bit with the latest sensor reading;
// it returns the laser level to output.
class LinkEngine {
public:
    explicit LinkEngine(uint16_t threshold = 2048) : threshold_(threshold) {}

    void setThreshold(uint16_t t) { threshold_ = t; }
    uint16_t threshold() const { return threshold_; }

    bool sendPayload(const uint8_t* p, size_t n);
    bool txBusy() const { return tx_.busy(); }

    bool tick(uint16_t adc, uint32_t nowMs);
    bool takeReceived(RxPacket& out);

private:
    static constexpr int QUEUE = 2;

    uint16_t threshold_;
    TxFramer tx_;
    RxDeframer rx_;
    PacketParser parser_;

    RxPacket work_ = {};
    bool framingBad_ = false;

    RxPacket queue_[QUEUE] = {};
    int head_ = 0;
    int count_ = 0;
};
