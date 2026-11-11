# Laser Chat (desktop app)

A chat window for the ESP32 laser link. Run it on both laptops, with each laptop's ESP32 plugged in over USB.

## Setup

You need Python 3.10 or newer.

### Fedora Linux

```bash
cd app
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
sudo usermod -aG dialout $USER     # one time; then log out and back in
.venv/bin/python laser_chat.py
```

Ports show up as `/dev/ttyUSB0` or `/dev/ttyACM0`.

### macOS

```bash
cd app
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python laser_chat.py
```

Ports show up as `/dev/cu.usbserial-…` or `/dev/cu.SLAB_USBtoUART`. If no port appears, install the USB driver for your board's USB chip (CH340 or CP210x) and plug the board in again.

### Windows

```bat
cd app
py -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python laser_chat.py
```

Ports show up as `COM3`, `COM4` and so on. If you're not sure which one is the ESP32, check **Device Manager → Ports (COM & LPT)**.

## Using it

1. Pick the ESP32's port in the dropdown (press ⟳ if you plugged the board in after starting the app), then press **Connect**.
2. The dot turns **green ("Online")** once the board answers. Amber means the port opened but the board hasn't answered yet: check the firmware is flashed, or press the board's reset button.
3. Type a message (up to 64 bytes; emoji count as 4 bytes) and press **Enter**.
4. Messages from the other laptop show up on the left. Clicking one shows its **measured** sensor readings in the strip at the bottom. Clicking one of your own messages shows its bits as a drawn waveform.

### Threshold

A reading **below** the threshold counts as "laser on". The default is 2048.

- In the signal strip, laser-on samples should sit clearly above the dashed line, which means a low reading, and laser-off samples clearly below it.
- If received messages show **⚠ corrupted**, look at the strip, move the threshold so the line sits halfway between the two levels, and press **Apply**.
- The board remembers the value after a reset.

## Optional: single-file app

Build this on each operating system you want to run it on:

```bash
.venv/bin/pip install pyinstaller
.venv/bin/pyinstaller --onefile --windowed --name LaserChat laser_chat.py
```

The app ends up in `dist/`.

## Tests

```bash
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/pytest
```
