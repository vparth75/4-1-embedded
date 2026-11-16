#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

constexpr int LASER_PIN = 32;
constexpr int RX_PIN = 33;
constexpr int THRESHOLD = 2048;
constexpr uint32_t BIT_US = 1000;       // 1 kbps
constexpr uint32_t MARK_US = 5000;
constexpr uint32_t GAP_US = 2000;
constexpr uint32_t QUIET_US = 12000;
constexpr uint8_t MAX_LEN = 64;

struct Message {
    uint8_t length;
    uint8_t data[MAX_LEN];
};

QueueHandle_t txQueue;
Message input{};
bool lineTooLong = false;
uint32_t lastOn;

void waitUntil(uint32_t deadline)
{
    while (static_cast<int32_t>(micros() - deadline) < 0) {}
}

void sendByte(uint8_t value, uint32_t &deadline)
{
    for (int bit = 7; bit >= 0; --bit) {
        waitUntil(deadline);
        digitalWrite(LASER_PIN, (value >> bit) & 1);
        deadline += BIT_US;
    }
}

void txTask(void *)
{
    Message msg;

    for (;;) {
        xQueueReceive(txQueue, &msg, portMAX_DELAY);

        // Leave enough dark time for the other receiver to re-arm.
        vTaskDelay(pdMS_TO_TICKS(25));

        digitalWrite(LASER_PIN, HIGH);
        delayMicroseconds(MARK_US);
        digitalWrite(LASER_PIN, LOW);

        uint32_t deadline = micros() + GAP_US;
        uint8_t checksum = msg.length;

        sendByte(msg.length, deadline);
        for (uint8_t i = 0; i < msg.length; ++i) {
            checksum ^= msg.data[i];
            sendByte(msg.data[i], deadline);
        }
        sendByte(checksum, deadline);

        waitUntil(deadline);
        digitalWrite(LASER_PIN, LOW);
    }
}

bool laserOn()
{
    return analogRead(RX_PIN) < THRESHOLD;
}

uint8_t readByte(uint32_t &deadline)
{
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        waitUntil(deadline);
        value = (value << 1) | laserOn();
        deadline += BIT_US;
    }
    return value;
}

void receiveOptical()
{
    if (!laserOn()) return;

    const uint32_t start = micros();
    const bool wasQuiet = start - lastOn >= QUIET_US;
    lastOn = start;
    if (!wasQuiet) return;

    while (laserOn()) {
        if (micros() - start > 7000) {
            lastOn = micros();
            return;
        }
    }

    const uint32_t fallingEdge = micros();
    lastOn = fallingEdge;
    if (fallingEdge - start < 4000) return;

    uint32_t deadline = fallingEdge + GAP_US + BIT_US / 2;
    Message msg;
    msg.length = readByte(deadline);

    if (msg.length == 0 || msg.length > MAX_LEN) {
        lastOn = micros();
        Serial.println("RX: invalid length");
        return;
    }

    uint8_t checksum = msg.length;
    for (uint8_t i = 0; i < msg.length; ++i) {
        msg.data[i] = readByte(deadline);
        checksum ^= msg.data[i];
    }

    const uint8_t receivedChecksum = readByte(deadline);
    lastOn = micros();

    if (receivedChecksum != checksum) {
        Serial.println("RX: checksum error");
        return;
    }

    Serial.print("RX: ");
    Serial.write(msg.data, msg.length);
    Serial.println();
}

void readTerminal()
{
    // Process one character per pass so pasted text doesn't starve ADC polling.
    const int c = Serial.read();
    if (c < 0) return;

    if (c == '\r' || c == '\n') {
        if (lineTooLong) {
            Serial.println("TX: line too long (64 bytes max)");
        } else if (input.length && xQueueSend(txQueue, &input, 0) != pdTRUE) {
            Serial.println("TX: queue full; line not sent");
        }
        input.length = 0;
        lineTooLong = false;
    } else if (input.length < MAX_LEN) {
        input.data[input.length++] = static_cast<uint8_t>(c);
    } else {
        lineTooLong = true;
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);
    analogReadResolution(12);

    txQueue = xQueueCreate(2, sizeof(Message));
    const BaseType_t txCore = 1 - xPortGetCoreID();
    if (!txQueue ||
        xTaskCreatePinnedToCore(txTask, "opticalTX", 3072, nullptr,
                                1, nullptr, txCore) != pdPASS) {
        Serial.println("TX initialization failed");
        for (;;) delay(1000);
    }

    lastOn = micros();
    Serial.println("Optical link ready. Type a line and press Enter.");
}

void loop()
{
    receiveOptical();
    readTerminal();
}