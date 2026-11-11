#pragma once
#include <stdint.h>
#include <stddef.h>

// Packet on the wire: [SYNC][LEN][PAYLOAD 0..64][CRC-8 over LEN+PAYLOAD]
constexpr uint8_t SYNC_BYTE = 0x7E;
constexpr size_t MAX_PAYLOAD = 64;
constexpr size_t MAX_PACKET = MAX_PAYLOAD + 3;
constexpr uint32_t PACKET_TIMEOUT_MS = 50;

// Writes the packet into `out` (at least MAX_PACKET bytes).
// Returns bytes written, or 0 if len > MAX_PAYLOAD.
size_t buildPacket(const uint8_t* payload, size_t len, uint8_t* out);

class PacketParser {
public:
    enum Result { NONE, DONE };

    // Feed one received byte. Returns DONE when a full packet (good or bad CRC) is ready.
    Result feed(uint8_t b, uint32_t nowMs);
    // Drop a half-received packet if bytes stopped arriving.
    void tick(uint32_t nowMs);

    const uint8_t* payload() const { return buf_; }
    size_t length() const { return len_; }
    bool ok() const { return ok_; }
    void reset();
    // True while waiting for a SYNC byte (no packet in progress).
    bool idle() const { return state_ == WAIT_SYNC; }

private:
    enum State { WAIT_SYNC, WAIT_LEN, PAYLOAD, WAIT_CRC };
    State state_ = WAIT_SYNC;
    uint8_t buf_[MAX_PAYLOAD] = {};
    size_t len_ = 0;
    size_t got_ = 0;
    bool ok_ = false;
    uint32_t lastMs_ = 0;
};
