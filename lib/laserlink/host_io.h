#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include "link_engine.h"

// JSON lines exchanged with the laptop app over USB serial.
// All returned strings are one line with no trailing newline.

constexpr const char* FW_VERSION = "laser-chat 1.0";
constexpr int HOST_BIT_US = 1000;

std::string bitsString(const uint8_t* p, size_t n);   // "01101000 01101001"
std::string helloEvent(uint16_t threshold);
std::string sentEvent(const uint8_t* p, size_t n);
std::string recvEvent(const RxPacket& pkt);
std::string errorEvent(const char* msg);

struct HostActions {
    bool sendHello;
    bool thresholdChanged;
    uint16_t newThreshold;
};

// Handles one command line from the app. Returns the reply line to send back.
std::string handleCommand(const std::string& line, LinkEngine& eng, HostActions& act);
