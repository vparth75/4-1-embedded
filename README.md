# Optical Communication in Guided Medium using ESP32: Laser Chat

Two laptops, each with an ESP32 that has a laser (GPIO32) and an IS203 light sensor (GPIO33). Text typed on one laptop travels as laser pulses (on-off keying, 1 kbps) and shows up on the other laptop. It works in both directions at once.

- `src/`, `lib/laserlink/`: firmware. **Flash the same firmware to both boards.**
- `app/`: desktop chat app for Fedora, macOS and Windows. See [app/README.md](app/README.md).
- `docs/superpowers/`: design and implementation plan.

## Flash the firmware

With the PlatformIO CLI:

```bash
pio run -e fm-devkit -t upload          # run once per board
```

(If `pio` isn't on your PATH, use `~/.platformio/penv/bin/pio`.) In VS Code you can use the PlatformIO **Upload** button instead.

Quick check without the app: open a serial monitor at 115200 on board A and type

```
{"cmd":"send","text":"hi"}
```

Board B's monitor should print `{"evt":"recv","ok":true,"text":"hi",...}`.

## How the link works

- Each character is sent as a **start bit (laser on)**, 8 data bits (most significant bit first), then a **stop bit (laser off)**. The laser stays off when idle.
- A message is `[0x7E][length][text, up to 64 bytes][CRC-8]`. The receiver spots damaged messages and shows them as corrupted.
- The sensor is sampled 8 times per bit. Each bit is read in the middle of its time slot, timed from the start edge, which keeps both boards in step.

## Tests

```bash
pio test -e native                      # firmware logic, runs on the laptop
cd app && .venv/bin/pytest              # desktop app
```
