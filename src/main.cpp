// Laser Chat firmware: identical on both boards.
// Laptop <-> ESP32: JSON lines over USB serial (115200).
// ESP32 <-> ESP32: UART-style frames over laser (GPIO32) and IS203 sensor (GPIO33).

#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <string>

#include "host_io.h"
#include "link_engine.h"

#define LASER_PIN 32
#define RX_PIN    33
#define BIT_US    1000

static const int64_t TICK_US = BIT_US / TICKS_PER_BIT;   // 125 us
static const size_t MAX_LINE = 512;

static LinkEngine engine;
static Preferences prefs;
static std::string lineBuf;
static bool lineOverflow = false;

// Runs forever on core 0: one tick = drive laser + sample sensor.
static void linkTask(void*)
{
    int64_t next = esp_timer_get_time();
    for (;;) {
        next += TICK_US;
        int64_t now;
        while ((now = esp_timer_get_time()) < next) {
        }
        if (now - next > TICK_US) {
            next = now;   // fell behind: resync instead of bursting
        }
        uint16_t adc = analogRead(RX_PIN);
        bool laser = engine.tick(adc, millis());
        digitalWrite(LASER_PIN, laser ? HIGH : LOW);
    }
}

static void sendLine(const std::string& s)
{
    if (!s.empty()) {
        Serial.write((const uint8_t*)s.data(), s.size());
        Serial.write('\n');
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);
    analogReadResolution(12);
    analogSetPinAttenuation(RX_PIN, ADC_11db);

    prefs.begin("laser", false);
    uint16_t thr = prefs.getUShort("thr", 2048);
    if (thr > 4095) {
        thr = 2048;
    }
    engine.setThreshold(thr);

    delay(200);
    sendLine(helloEvent(engine.threshold()));

    // The link task never sleeps, so the core 0 idle watchdog must be off.
    disableCore0WDT();
    xTaskCreatePinnedToCore(linkTask, "link", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr, 0);
}

void loop()
{
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (!lineOverflow && !lineBuf.empty()) {
                HostActions act;
                sendLine(handleCommand(lineBuf, engine, act));
                if (act.thresholdChanged) {
                    prefs.putUShort("thr", act.newThreshold);
                }
            }
            lineBuf.clear();
            lineOverflow = false;
            continue;
        }
        if (lineBuf.size() < MAX_LINE) {
            lineBuf += c;
        } else {
            lineOverflow = true;
        }
    }

    static RxPacket pkt;
    while (engine.takeReceived(pkt)) {
        sendLine(recvEvent(pkt));
    }

    delay(1);
}
