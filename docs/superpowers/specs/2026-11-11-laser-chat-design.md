# Laser Chat: two-laptop optical messaging, design

Date: 2026-11-11
Status: reviewed, awaiting approval

## Intent

This is a university embedded-hardware project, "Optical Communication in Guided Medium using ESP32". The project report is at `../Embedded REPORT.pdf`. The hardware already sends light pulses using on-off keying (OOK): laser on for `1`, laser off for `0`. Today the firmware (`src/main.cpp`) is a **single-board loopback**. One ESP32 switches its own laser and reads its own sensor in the same bit slot, then prints TX, RX and PASS/FAIL to a serial monitor.

The goal is a demo that looks and works like a real communication link. **There are two laptops, each with its own ESP32. Someone types a message on laptop A, it travels as laser pulses, and it appears on laptop B in a proper desktop chat app. Replies work the same way in the other direction.** The app also shows the **sensor signal as the receiving board measured it**, so an evaluator can see that light is being detected and not just believe it.

## Setup (as the user described it)

- Two laptops. Each one has its own ESP32 connected over USB.
- Each ESP32 has a laser (GPIO32, switched through an NPN transistor) and an IS203 light sensor (GPIO33, read with the ADC).
- Board A's laser points at board B's sensor, and board B's laser points at board A's sensor, through **two separate light paths** (confirmed by the user). A board's own laser does not reach its own sensor. The firmware must not depend on which board is which.
- PlatformIO project, `board = esp32dev` (ESP32 DevKit / NodeMCU boards), Arduino framework.
- Known-good values from the current code:
  - 1 kbps (`BIT_US = 1000`)
  - 12-bit ADC
  - `THRESHOLD = 2048`
  - **a bit is `1` when `analogRead(33) < THRESHOLD`**: light pulls the reading low.

## Goals

1. Type a message on either laptop and it appears on the other one.
2. The link works in both directions, and a board doesn't lose a message that arrives while it is sending (the two light paths are physically separate).
3. Each board finds the start of every character and message on its own, with no shared clock and no help from the sending board.
4. A message corrupted in transit is shown as corrupted. It is never shown as if it arrived correctly.
5. A desktop app (not a browser page) that runs on **Fedora Linux, macOS and Windows**.
6. The app shows the **measured** sensor signal for received messages (option C + measured, chosen by the user).

## Non-goals

- No hardware or circuit changes.
- No higher bit rate; stay at about 1 kbps.
- No encryption, file transfer, group chat or more than two boards.
- No chat history saved after the app closes.
- No installers or signed app bundles (see "Running it" below).

## Anti-goals (count as failure even if it technically works)

- A garbled message shown as if it arrived fine.
- The app freezing or crashing when the ESP32 is unplugged or reset, or when a port goes away.
- A board that only works as sender or only as receiver depending on which one is flashed. **Both boards run identical firmware.**
- Setup on demo day that needs drivers or command-line steps that weren't written down.

---

## Part 1: Firmware (`src/main.cpp`, replaces the loopback)

### Optical framing (my call)

The current code depends on one board knowing when each bit starts. Two boards don't share that knowledge, so each byte is framed UART-style:

- **Idle: laser OFF** (reads as `0`).
- **Start bit: `1`** (laser ON). The receiver watches for the idle→ON edge.
- **8 data bits, MSB first** (the same bit order as today).
- **Stop bit: `0`** (laser OFF). This guarantees an ON edge before the next start bit.
- The receiver samples each bit **at the middle of its slot**, timed from the detected start edge, and it checks again for each new byte so clock drift doesn't build up.

Messages are wrapped in a small packet so the receiver knows where a message starts and ends and whether it's intact:

```
[SYNC 0xAA?] [LEN 1 byte] [PAYLOAD LEN bytes, max 64] [CRC-8 over LEN+PAYLOAD]
```

The exact sync byte(s) and CRC polynomial are left to the builder. Requirements:
- The receiver must recover if a packet is cut short. If bytes stop arriving partway through a packet, it times out and goes back to waiting for a new packet.
- If the CRC check fails, the packet still goes to the app, marked `ok: false`, with the bytes as received. The app shows it as corrupted (goal 4).

### Sending and receiving at the same time

Receiving can't pause while the board sends (goal 2). **The builder decides how.** One reasonable approach: a single fixed-rate timing loop pinned to one core (for example 8 ticks per bit). Each tick moves the TX bit stream forward and takes one ADC sample for RX. That replaces `delayMicroseconds` blocking. Keep in mind that `analogRead` is not safe to call inside an interrupt in some ESP32 Arduino core versions.

### Measured signal capture

For every received packet, the firmware sends the app **one ADC reading per bit**: the mid-bit sample it decided on. This covers start, data and stop bits for every byte. The builder may send more samples per bit if bandwidth allows. 64 bytes × 10 bits × 2 bytes per reading is about 1.3 KB, which is small compared to USB serial at 115200.

### USB serial protocol (ESP32 ↔ app)

115200 baud, **one JSON object per line** (my call: it's easy to parse on both sides and easy to read in a plain serial monitor while debugging).

App → ESP32:
```json
{"cmd":"send","text":"hello"}
```
ESP32 → app (the field names below are a guide; the builder may refine them):
```json
{"evt":"hello","fw":"laser-chat 1.0","bit_us":1000,"threshold":2048}
{"evt":"sent","text":"hello","bits":"0110100001100101..."}
{"evt":"recv","ok":true,"text":"hi","bits":"...","adc":[312,3890,401,...]}
{"evt":"error","msg":"message too long"}
```
- `hello` is sent at boot and again when the app asks for it (`{"cmd":"hello"}`), so the app can confirm it's talking to the right firmware.
- `{"cmd":"set_threshold","value":N}` changes the RX threshold at runtime and **saves it in flash (NVS)** so it survives a reset; the board replies with a fresh `hello`. Default is 2048 when nothing is saved. *(my call; keeps both boards on identical firmware while still allowing per-board tuning on demo day)*
- Text longer than 64 bytes is rejected with an `error`, not cut off without warning.
- The firmware doesn't need its own human-friendly serial monitor mode.

### Testing the firmware

Packet building, packet parsing, CRC and bit framing go in pure functions with no Arduino calls, tested with PlatformIO `native` unit tests. The optical link itself can only be checked on the real hardware, by the user.

---

## Part 2: Desktop app (`app/`)

### Stack (my call)

**Python 3.10+, PySide6 (Qt), pyserial.** It runs the same on Fedora, macOS and Windows and looks native.

### Window layout (option C, chosen by the user)

The mockup is at `.superpowers/brainstorm/*/content/layout.html`, card C.

- **Top bar:** app name; a serial port dropdown with a refresh option (shows `/dev/ttyUSB*`/`ttyACM*`, `/dev/cu.*` or `COMx`); a Connect/Disconnect button; a status dot (green = connected and `hello` received, amber = port open but no `hello`, red = disconnected).
- **Chat area:**
  - Sent messages: right side, red bubbles. Received messages: left side, grey bubbles.
  - Every bubble shows the time. Sent bubbles show the bits that went out.
  - Corrupted received messages get a clear warning style and label, for example "⚠ corrupted (CRC failed)".
- **Signal strip:** shows the **measured ADC readings** of the selected received message. It defaults to the most recent one, and clicking a received bubble selects it. The strip includes:
  - a horizontal **threshold line**
  - each sample drawn as a step/point trace on the 0–4095 scale
  - optional light shading that marks start, data and stop bits
  
  *My call:* sent messages have no measured signal on the sender's side, because the sender's sensor watches the other laser. Clicking a sent bubble shows its bits as a **drawn** waveform, clearly labeled "sent (drawn)".
- **Input row:** text field limited to 64 characters with a counter, plus a Send button. Enter sends. Input is disabled while disconnected.
- Dark theme.

### Behavior

- Serial reading runs off the GUI thread. Unplugging, a board reset or a port error switches the status to disconnected. The app never freezes or crashes (anti-goal).
- Lines that aren't valid JSON get ignored, which also covers boot noise from the ESP32.
- A sent message shows up in the chat only after the board confirms it with `sent`. Until then it appears as pending.
- A small **threshold control** (spin box or slider, 0–4095, with an Apply button) lives in the top bar or a settings popover. It shows the board's current value from `hello` and sends `set_threshold`. The threshold line in the signal strip uses this value. *(my call)*
- **No delivery confirmation.** "Sent" means "my board transmitted it". A packet whose sync or length byte is destroyed never reaches the other app. Acknowledgements and retries are out of scope. *(my call)*

### Running it (my call)

- `app/requirements.txt` with plain run steps: `pip install -r requirements.txt`, then `python laser_chat.py`.
- A README section for each OS:
  - Fedora: add the user to the `dialout` group.
  - macOS: CH340/CP210x USB driver if needed, and the `/dev/cu.*` port names.
  - Windows: finding the COM port.
- Optional and nice to have: a PyInstaller recipe for building a one-file app on each OS. It isn't required.

### Testing the app

Parsing and state logic are separated from Qt widgets and covered by unit tests. A fake serial source replays recorded JSON lines, so the UI can be exercised without hardware.

---

## Left to the builder

- Sync byte(s), CRC-8 polynomial and packet timeout.
- How the firmware handles timing for sending and receiving at the same time.
- Samples per bit for the measured signal, if more than one.
- Exact JSON field names, as long as they match the meanings above.
- Code layout inside `app/` and `src/`.

## Open risks

- **The two-board link is untested with this framing.** The 2048 threshold was tuned for one board reading its own laser. It may need adjusting per board, which is why it can be set from the app and is saved on the board.
- If a board's own laser does leak into its own sensor after all, it would decode its own packets. That would show up right away in the first test, and the fix would be to ignore the sensor while sending.
