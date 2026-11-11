#include "host_io.h"
#include <ArduinoJson.h>

static std::string toLine(const JsonDocument& doc)
{
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string bitsString(const uint8_t* p, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            s += ' ';
        }
        for (int b = 7; b >= 0; b--) {
            s += ((p[i] >> b) & 1) ? '1' : '0';
        }
    }
    return s;
}

std::string helloEvent(uint16_t threshold)
{
    JsonDocument doc;
    doc["evt"] = "hello";
    doc["fw"] = FW_VERSION;
    doc["bit_us"] = HOST_BIT_US;
    doc["threshold"] = threshold;
    return toLine(doc);
}

std::string sentEvent(const uint8_t* p, size_t n)
{
    JsonDocument doc;
    doc["evt"] = "sent";
    doc["text"] = std::string((const char*)p, n);
    doc["bits"] = bitsString(p, n);
    return toLine(doc);
}

std::string recvEvent(const RxPacket& pkt)
{
    JsonDocument doc;
    doc["evt"] = "recv";
    doc["ok"] = pkt.ok;
    doc["text"] = std::string((const char*)pkt.payload, pkt.len);
    doc["bits"] = bitsString(pkt.payload, pkt.len);
    JsonArray adc = doc["adc"].to<JsonArray>();
    for (size_t i = 0; i < pkt.adcCount; i++) {
        adc.add(pkt.adc[i]);
    }
    return toLine(doc);
}

std::string errorEvent(const char* msg)
{
    JsonDocument doc;
    doc["evt"] = "error";
    doc["msg"] = msg;
    return toLine(doc);
}

std::string handleCommand(const std::string& line, LinkEngine& eng, HostActions& act)
{
    act = HostActions{};

    JsonDocument doc;
    if (deserializeJson(doc, line) || !doc.is<JsonObject>()) {
        return errorEvent("bad command");
    }
    const char* cmd = doc["cmd"] | "";
    std::string c(cmd);

    if (c == "send") {
        JsonVariant t = doc["text"];
        if (!t.is<const char*>()) {
            return errorEvent("bad command");
        }
        std::string text = t.as<std::string>();
        if (text.empty()) {
            return errorEvent("empty message");
        }
        if (text.size() > MAX_PAYLOAD) {
            return errorEvent("message too long");
        }
        if (!eng.sendPayload((const uint8_t*)text.data(), text.size())) {
            return errorEvent("busy");
        }
        return sentEvent((const uint8_t*)text.data(), text.size());
    }

    if (c == "hello") {
        act.sendHello = true;
        return helloEvent(eng.threshold());
    }

    if (c == "set_threshold") {
        JsonVariant v = doc["value"];
        if (!v.is<int>()) {
            return errorEvent("bad threshold");
        }
        int value = v.as<int>();
        if (value < 0 || value > 4095) {
            return errorEvent("bad threshold");
        }
        eng.setThreshold((uint16_t)value);
        act.thresholdChanged = true;
        act.newThreshold = (uint16_t)value;
        return helloEvent(eng.threshold());
    }

    return errorEvent("bad command");
}
