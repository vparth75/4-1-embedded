#include <Arduino.h>

#define LASER_PIN 32
#define RX_PIN    33

#define BIT_US    1000
#define THRESHOLD 2048

#define MAX_MESSAGE 64

String inputMessage = "";


// ============================================================
// OPTICAL BIT
// ============================================================

bool transmitAndReceiveBit(bool txBit)
{
    digitalWrite(LASER_PIN, txBit ? HIGH : LOW);

    delayMicroseconds(BIT_US / 2);

    int adc = analogRead(RX_PIN);

    bool rxBit = (adc < THRESHOLD);

    delayMicroseconds(BIT_US / 2);

    return rxBit;
}


// ============================================================
// BYTE
// ============================================================

uint8_t transmitAndReceiveByte(uint8_t txByte)
{
    uint8_t rxByte = 0;

    for (int bit = 7; bit >= 0; bit--)
    {
        bool txBit = (txByte >> bit) & 1;

        bool rxBit = transmitAndReceiveBit(txBit);

        rxByte <<= 1;

        if (rxBit)
        {
            rxByte |= 1;
        }
    }

    return rxByte;
}


// ============================================================
// SEND MESSAGE
// ============================================================

void opticalLoopback(String message)
{
    Serial.println();
    Serial.println("================================");
    Serial.println("TRANSMITTING");
    Serial.println("================================");

    Serial.print("TX: ");
    Serial.println(message);

    String receivedMessage = "";

    int totalBits = 0;
    int errors = 0;

    for (int i = 0; i < message.length(); i++)
    {
        uint8_t txByte = (uint8_t)message[i];
        uint8_t rxByte = 0;

        for (int bit = 7; bit >= 0; bit--)
        {
            bool txBit = (txByte >> bit) & 1;

            bool rxBit = transmitAndReceiveBit(txBit);

            rxByte <<= 1;

            if (rxBit)
            {
                rxByte |= 1;
            }

            totalBits++;

            if (txBit != rxBit)
            {
                errors++;
            }
        }

        receivedMessage += (char)rxByte;
    }

    digitalWrite(LASER_PIN, LOW);

    Serial.println();
    Serial.println("================================");
    Serial.println("DECODED RESULT");
    Serial.println("================================");

    Serial.print("TX: ");
    Serial.println(message);

    Serial.print("RX: ");
    Serial.println(receivedMessage);

    Serial.print("Bits: ");
    Serial.println(totalBits);

    Serial.print("Errors: ");
    Serial.println(errors);

    if (receivedMessage == message && errors == 0)
    {
        Serial.println("RESULT: PASS");
    }
    else
    {
        Serial.println("RESULT: FAIL");
    }

    Serial.println();
    Serial.println("Type another message:");
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);

    analogReadResolution(12);

    delay(500);

    Serial.println();
    Serial.println("================================");
    Serial.println(" SAME ESP32 OPTICAL TERMINAL");
    Serial.println("================================");
    Serial.println();
    Serial.println("TX: GPIO32 -> LASER");
    Serial.println("RX: GPIO33 <- IS203");
    Serial.println("RATE: 1 kbps");
    Serial.println();
    Serial.println("READY");
    Serial.println("Type your message below and press ENTER.");
    Serial.println();
}


// ============================================================
// TERMINAL
// ============================================================

void loop()
{
    if (Serial.available() > 0)
    {
        char c = Serial.read();

        // Print the character back so we know
        // the ESP32 actually received it.
        if (c != '\r' && c != '\n')
        {
            Serial.print(c);
        }

        // ENTER
        if (c == '\n' || c == '\r')
        {
            if (inputMessage.length() > 0)
            {
                String message = inputMessage;

                inputMessage = "";

                Serial.println();

                opticalLoopback(message);
            }
        }

        // Normal character
        else
        {
            if (inputMessage.length() < MAX_MESSAGE)
            {
                inputMessage += c;
            }
        }
    }
}