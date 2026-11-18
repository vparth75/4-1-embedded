#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

constexpr int LASER_PIN = 32;
constexpr int RX_PIN = 33;
constexpr int THRESHOLD = 2048;

constexpr uint32_t BIT_US = 1000;
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
uint32_t lastOn = 0;

// Wait for an absolute timestamp to avoid accumulated timing drift.
void waitUntil(uint32_t time)
{
    while ((int32_t)(micros() - time) < 0) {}
}

// Transmit one byte, most-significant bit first.
void sendByte(uint8_t value, uint32_t &time)
{
    for (int bit = 7; bit >= 0; --bit) {
        waitUntil(time);
        digitalWrite(LASER_PIN, (value >> bit) & 1);
        time += BIT_US;
    }
}

// Runs independently on the other ESP32 core.
void transmit(void *)
{
    Message msg;

    while (true) {
        xQueueReceive(txQueue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(25));

        // Start marker followed by a dark gap.
        digitalWrite(LASER_PIN, HIGH);
        delayMicroseconds(MARK_US);
        digitalWrite(LASER_PIN, LOW);

        uint32_t time = micros() + GAP_US;
        uint8_t checksum = msg.length;

        sendByte(msg.length, time);

        for (uint8_t i = 0; i < msg.length; ++i) {
            checksum ^= msg.data[i];
            sendByte(msg.data[i], time);
        }

        sendByte(checksum, time);
        waitUntil(time);
        digitalWrite(LASER_PIN, LOW);
    }
}

// The IS203 output is active-low.
bool laserOn()
{
    return analogRead(RX_PIN) < THRESHOLD;
}

// Sample eight bits at their expected centers.
uint8_t readByte(uint32_t &time)
{
    uint8_t value = 0;

    for (int i = 0; i < 8; ++i) {
        waitUntil(time);
        value = (value << 1) | laserOn();
        time += BIT_US;
    }

    return value;
}

// Detect and decode an incoming optical packet.
void receive()
{
    if (!laserOn()) return;

    uint32_t start = micros();
    bool wasQuiet = start - lastOn >= QUIET_US;
    lastOn = start;

    if (!wasQuiet) return;

    // Accept a synchronization pulse lasting 4–7 ms.
    while (laserOn()) {
        if (micros() - start > 7000) {
            lastOn = micros();
            return;
        }
    }

    uint32_t fallingEdge = micros();
    lastOn = fallingEdge;

    if (fallingEdge - start < 4000) return;

    // Sample the first data bit after the 2 ms gap.
    uint32_t time = fallingEdge + GAP_US + BIT_US / 2;

    Message msg;
    msg.length = readByte(time);

    if (msg.length == 0 || msg.length > MAX_LEN) {
        lastOn = micros();
        Serial.println("RX: invalid length");
        return;
    }

    uint8_t checksum = msg.length;

    for (uint8_t i = 0; i < msg.length; ++i) {
        msg.data[i] = readByte(time);
        checksum ^= msg.data[i];
    }

    uint8_t receivedChecksum = readByte(time);
    lastOn = micros();

    if (receivedChecksum != checksum) {
        Serial.println("RX: checksum error");
        return;
    }

    Serial.print("RX: ");
    Serial.write(msg.data, msg.length);
    Serial.println();
}

// Read terminal input without blocking optical reception.
void readTerminal()
{
    int c = Serial.read();
    if (c < 0) return;

    if (c == '\r' || c == '\n') {
        if (lineTooLong) {
            Serial.println("TX: line too long (64 bytes max)");
        }
        else if (input.length &&
                 xQueueSend(txQueue, &input, 0) != pdTRUE) {
            Serial.println("TX: queue full; line not sent");
        }

        input.length = 0;
        lineTooLong = false;
        return;
    }

    if (input.length < MAX_LEN) {
        input.data[input.length++] = (uint8_t)c;
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

    if (!txQueue ||
        xTaskCreatePinnedToCore(
            transmit, "opticalTX", 3072,
            nullptr, 1, nullptr,
            1 - xPortGetCoreID()
        ) != pdPASS) {

        Serial.println("TX initialization failed");
        for (;;) delay(1000);
    }

    lastOn = micros();
    Serial.println("Optical link ready. Type a line and press Enter.");
}

void loop()
{
    receive();
    readTerminal();
}