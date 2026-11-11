# Laser Chat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two-laptop optical chat: identical ESP32 firmware that frames, sends and receives packets over laser/IS203 at 1 kbps, full duplex, plus a cross-platform PySide6 desktop app showing chat bubbles and the measured sensor signal.

**Architecture:** Firmware protocol logic lives in a pure C++ library (`lib/laserlink/`, no Arduino headers) unit-tested with PlatformIO `native`; `src/main.cpp` only wires it to GPIO/ADC/NVS/Serial. A link task pinned to core 0 runs a fixed 8-ticks-per-bit loop that drives the laser and samples the ADC every tick, so TX and RX run at the same time. The app (`app/laserchat/`) separates pure protocol/model code (pytest) from Qt widgets (offscreen smoke tests with a fake serial source).

**Tech Stack:** PlatformIO (espressif32, Arduino, board `fm-devkit`), C++17, ArduinoJson 7, Unity; Python 3.10+, PySide6, pyserial, pytest.

**Spec:** `docs/superpowers/specs/2026-11-11-laser-chat-design.md`

## Global Constraints

- Pins: laser `GPIO32` (`LASER_PIN 32`), sensor `GPIO33` (`RX_PIN 33`).
- Bit time `BIT_US = 1000` (1 kbps); 12-bit ADC; default `THRESHOLD = 2048`; **bit is `1` when `adc < threshold`**.
- Byte framing: idle laser OFF (`0`), start bit `1`, 8 data bits MSB first, stop bit `0`.
- Packet: `[SYNC 0x7E][LEN][PAYLOAD 0..64][CRC-8 over LEN+PAYLOAD]`, max payload 64 bytes.
- Both boards run identical firmware; no board role setting.
- USB serial 115200 baud, one JSON object per line, `\n` terminated.
- Text longer than 64 **bytes** (UTF-8) is rejected with `{"evt":"error",...}`, never truncated.
- Threshold set via `{"cmd":"set_threshold","value":N}`, `0 ≤ N ≤ 4095`, stored in NVS (Preferences namespace `"laser"`, key `"thr"`), default 2048.
- App: Python ≥3.10, PySide6, pyserial; must run on Fedora, macOS, Windows; dark theme; no history persistence; no delivery confirmation.
- PlatformIO CLI on the dev machine: `~/.platformio/penv/bin/pio` (not on PATH).
- Work on branch `laser-chat`, not `main`.

## Review Focus

1. **USB unplugged / board reset mid-session** → app shows red "Disconnected", stays usable, can reconnect; never freezes. Test: Task 6 `test_worker_emits_disconnected_on_serial_error`, `test_window_disconnect_disables_input`.
2. **Multi-byte characters (emoji, accents)** → 64 is a byte limit; app counter and send-blocking count UTF-8 bytes, firmware rejects >64 bytes. Tests: Task 4 `test_cmd_send_rejects_over_64_utf8_bytes`; Task 3 `test_handle_send_too_long`.
3. **Boot garbage / half lines / non-JSON on serial** → ignored silently. Test: Task 4 `test_parse_line_ignores_garbage`.
4. **Laser stuck ON or ambient light saturating sensor** → receiver emits at most one framing-error byte, then waits for idle before the next start; never floods. Test: Task 2 `test_constant_light_yields_no_flood`.
5. **Both laptops send at the same moment** → each board still receives the other's packet intact while transmitting. Test: Task 2 `test_engine_full_duplex`.

---

## File Structure

```
platformio.ini                         (modify: add native env, lib_deps)
lib/laserlink/
  library.json
  crc8.h / crc8.cpp                    CRC-8 (poly 0x07, init 0x00)
  packet.h / packet.cpp                buildPacket, PacketParser
  framer.h / framer.cpp                TxFramer, RxDeframer
  link_engine.h / link_engine.cpp      LinkEngine: one tick = TX level + RX sample
  host_io.h / host_io.cpp              JSON command handling / event encoding (ArduinoJson)
src/main.cpp                           (rewrite) hardware glue, link task, serial task
test/test_native/test_main.cpp         Unity tests for lib/laserlink
app/
  laser_chat.py                        entry point
  requirements.txt, requirements-dev.txt, README.md
  laserchat/__init__.py
  laserchat/protocol.py                parse_line, Event dataclasses, cmd_* builders
  laserchat/model.py                   ChatMessage, ChatModel
  laserchat/serial_worker.py           SerialWorker (QThread), list_ports
  laserchat/signal_view.py             SignalView widget
  laserchat/chat_view.py               ChatView + bubble widget
  laserchat/main_window.py             MainWindow, theme
  tests/test_protocol.py, tests/test_model.py, tests/test_widgets.py, tests/test_window.py
.gitignore                             (modify: .superpowers/, app/.venv, __pycache__)
```

---

### Task 1: Packet layer (CRC-8, build, parse) + native test env

**Files:**
- Create: `lib/laserlink/library.json`, `lib/laserlink/crc8.{h,cpp}`, `lib/laserlink/packet.{h,cpp}`
- Create: `test/test_native/test_main.cpp`
- Modify: `platformio.ini`, `.gitignore`

**Interfaces:**
- Produces:
  - `uint8_t crc8(const uint8_t* data, size_t len);` — poly `0x07`, init `0x00`, no reflection, no final XOR.
  - `constexpr uint8_t SYNC_BYTE = 0x7E; constexpr size_t MAX_PAYLOAD = 64; constexpr size_t MAX_PACKET = MAX_PAYLOAD + 3; constexpr uint32_t PACKET_TIMEOUT_MS = 50;`
  - `size_t buildPacket(const uint8_t* payload, size_t len, uint8_t* out);` — returns bytes written, `0` if `len > MAX_PAYLOAD`.
  - `class PacketParser { public: enum Result { NONE, DONE }; Result feed(uint8_t b, uint32_t nowMs); void tick(uint32_t nowMs); const uint8_t* payload() const; size_t length() const; bool ok() const; void reset(); };` — waits for `SYNC_BYTE`, reads LEN (LEN > 64 → reset to waiting), payload, CRC; `DONE` on CRC byte with `ok()` = CRC match. If more than `PACKET_TIMEOUT_MS` passes between bytes mid-packet, `tick`/`feed` reset to waiting for sync (a byte arriving after timeout is treated as a fresh start).

- [ ] **Step 1: Create branch and native env**

```bash
git checkout -b laser-chat
```
`platformio.ini`: keep `[env:fm-devkit]`, add `lib_deps = bblanchon/ArduinoJson@^7.2.0` to it; add
```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
lib_deps = bblanchon/ArduinoJson@^7.2.0
```
`library.json`: `{"name":"laserlink","version":"1.0.0","build":{"flags":"-std=gnu++17"}}`. Append `.superpowers/`, `app/.venv/`, `__pycache__/`, `.pytest_cache/` to `.gitignore`.

- [ ] **Step 2: Write failing tests** in `test/test_native/test_main.cpp` (Unity, with `main()` calling `UNITY_BEGIN/RUN_TEST/UNITY_END`):

```cpp
void test_crc8_known_vector() {           // CRC-8/SMBUS check value
  const uint8_t s[] = {'1','2','3','4','5','6','7','8','9'};
  TEST_ASSERT_EQUAL_HEX8(0xF4, crc8(s, 9));
}
void test_build_packet_layout() {
  uint8_t out[MAX_PACKET]; const uint8_t p[] = {'h','i'};
  TEST_ASSERT_EQUAL(5, buildPacket(p, 2, out));
  TEST_ASSERT_EQUAL_HEX8(0x7E, out[0]); TEST_ASSERT_EQUAL(2, out[1]);
  TEST_ASSERT_EQUAL_HEX8(crc8(out + 1, 3), out[4]);
}
void test_build_packet_rejects_65() { uint8_t in[65] = {0}, out[80]; TEST_ASSERT_EQUAL(0, buildPacket(in, 65, out)); }
void test_parser_roundtrip_ok();          // feed built "hello" packet → DONE, ok()==true, payload=="hello"
void test_parser_bad_crc();               // flip one payload bit → DONE, ok()==false, length()==5
void test_parser_skips_noise_before_sync(); // feed 0x00,0xFF,0x13 then packet → DONE ok
void test_parser_timeout_resets();        // feed sync+len=5+"he" at t=0..2, then nothing; tick(100); feed full packet at t=200 → DONE ok
void test_parser_len_over_64_resets();    // sync, 0x41 → NONE; then valid packet → DONE ok
void test_parser_empty_payload();         // len 0 packet → DONE, ok, length 0
```

- [ ] **Step 3: Run to verify failure**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: compile errors (`crc8` / `buildPacket` undeclared).

- [ ] **Step 4: Implement** `crc8.cpp`, `packet.cpp` per the Interfaces block. Headers include only `<stdint.h>`, `<stddef.h>`.

- [ ] **Step 5: Run to verify pass**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all 9 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini .gitignore lib/laserlink test/test_native
git commit -m "feat(fw): packet layer with CRC-8 and native tests"
```

---

### Task 2: Bit framing and link engine (full duplex)

**Files:**
- Create: `lib/laserlink/framer.{h,cpp}`, `lib/laserlink/link_engine.{h,cpp}`
- Modify: `test/test_native/test_main.cpp`

**Interfaces:**
- Consumes: `buildPacket`, `PacketParser`, constants from Task 1.
- Produces:
  - `constexpr int TICKS_PER_BIT = 8; constexpr int BITS_PER_FRAME = 10;`
  - `class TxFramer { public: bool load(const uint8_t* bytes, size_t n); bool busy() const; bool tick(); };` — `load` copies up to `MAX_PACKET` bytes, returns false if busy or too big. `tick()` returns laser level for this tick (true = ON); each bit held for `TICKS_PER_BIT` ticks; frame = start(1), d7..d0, stop(0); returns false when idle.
  - `struct RxByte { uint8_t value; bool framingOk; uint16_t adc[BITS_PER_FRAME]; };`
  - `class RxDeframer { public: bool tick(bool level, uint16_t adc, RxByte& out); };` — state machine: IDLE waits until it has seen level 0 then a 0→1 edge; START waits `TICKS_PER_BIT/2` ticks and re-checks level (0 → back to IDLE, glitch); then samples every `TICKS_PER_BIT` ticks for d7..d0 and stop, recording the sampled `adc` per bit. After the stop sample returns true with `out` filled; `framingOk = (stop level == 0)`. After a framing error it requires level 0 again before accepting a new edge (no flood under constant light).
  - `class LinkEngine { public: explicit LinkEngine(uint16_t threshold = 2048); void setThreshold(uint16_t); uint16_t threshold() const; bool sendPayload(const uint8_t* p, size_t n); bool txBusy() const; bool tick(uint16_t adc, uint32_t nowMs); bool takeReceived(RxPacket& out); };` — `tick` converts `adc < threshold` to a level, feeds `RxDeframer` and `PacketParser`, returns laser level from `TxFramer`. Completed packets go into a 2-slot ring buffer read with `takeReceived`.
  - `struct RxPacket { uint8_t payload[MAX_PAYLOAD]; size_t len; bool ok; uint16_t adc[MAX_PACKET * BITS_PER_FRAME]; size_t adcCount; };` — `adc` holds mid-bit samples for every frame of the packet starting at the SYNC byte; a framing error on any byte sets `ok = false` once the packet completes.

- [ ] **Step 1: Write failing tests** (helper: `simulate(TxFramer&, RxDeframer&, n ticks)` feeding TX level straight into RX with adc = level ? 300 : 3900):

```cpp
void test_tx_frame_shape();               // load {0xA5}; ticks 0..79 → level per bit = 1,1,0,1,0,0,1,0,1,0 (start, 0xA5 MSB-first, stop), each 8 ticks; then false; busy()==false
void test_tx_idle_is_off();               // fresh TxFramer: 20 ticks all false
void test_rx_decodes_byte();              // TX 0x48 → RX one RxByte value 0x48, framingOk, adc[0]==300, adc[9]==3900
void test_rx_two_bytes_back_to_back();    // TX {0x00,0xFF} → 0x00 then 0xFF, both framingOk
void test_rx_ignores_short_glitch();      // level 1 for 2 ticks then 0 → no byte
void test_rx_tolerates_clock_drift();     // resample TX stream at 8.4 ticks/bit (~5% drift) → bytes still decode
void test_constant_light_yields_no_flood(); // 2000 ticks of level 1 after idle → at most 1 RxByte, framingOk==false
void test_engine_loopback_packet();       // engine A sends "hello"; feed A's level into engine B as adc → B.takeReceived: ok, "hello", adcCount==8*10
void test_engine_full_duplex();           // A sends "ping", B sends "pong" starting same tick; cross-feed for enough ticks → A receives "pong", B receives "ping", both ok
void test_engine_threshold_inverted_logic(); // threshold 1000: adc 900 → level 1, adc 1100 → level 0
```

- [ ] **Step 2: Run to verify failure**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: compile errors for new types.

- [ ] **Step 3: Implement** `framer.cpp` and `link_engine.cpp` per Interfaces. No dynamic allocation; fixed arrays only.

- [ ] **Step 4: Run to verify pass**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all Task 1 + Task 2 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/laserlink test/test_native
git commit -m "feat(fw): UART-style optical framing and full-duplex link engine"
```

---

### Task 3: Host JSON protocol (commands in, events out)

**Files:**
- Create: `lib/laserlink/host_io.{h,cpp}`
- Modify: `test/test_native/test_main.cpp`

**Interfaces:**
- Consumes: `LinkEngine`, `RxPacket`, `MAX_PAYLOAD`.
- Produces:
  - `constexpr const char* FW_VERSION = "laser-chat 1.0";`
  - `std::string bitsString(const uint8_t* p, size_t n);` — 8 chars per byte, bytes separated by one space, e.g. `"01101000 01101001"`.
  - `std::string helloEvent(uint16_t threshold);` → `{"evt":"hello","fw":"laser-chat 1.0","bit_us":1000,"threshold":2048}`
  - `std::string sentEvent(const uint8_t* p, size_t n);` → `{"evt":"sent","text":...,"bits":...}`
  - `std::string recvEvent(const RxPacket& pkt);` → `{"evt":"recv","ok":bool,"text":...,"bits":...,"adc":[...]}`. `text` is the payload bytes as a JSON string (invalid UTF-8 is passed through by ArduinoJson; the app decodes with replacement).
  - `std::string errorEvent(const char* msg);`
  - `struct HostActions { bool sendHello; bool thresholdChanged; uint16_t newThreshold; };`
  - `std::string handleCommand(const std::string& line, LinkEngine& eng, HostActions& act);` — returns the event line to write back right away (may be empty). `send`: text bytes > 64 → `errorEvent("message too long")`; engine busy → `errorEvent("busy")`; else `sendPayload` and return `sentEvent`. `hello` → `act.sendHello = true`, returns `helloEvent`. `set_threshold` with integer 0..4095 → `eng.setThreshold`, `act.thresholdChanged = true`, returns `helloEvent`; out of range or missing → `errorEvent("bad threshold")`. Invalid JSON / unknown cmd → `errorEvent("bad command")`. All strings have no trailing newline; caller adds `\n`.

- [ ] **Step 1: Write failing tests**

```cpp
void test_bits_string();                  // {'h','i'} → "01101000 01101001"
void test_hello_event_fields();           // parse helloEvent(1500) with ArduinoJson: evt=="hello", threshold==1500, bit_us==1000, fw=="laser-chat 1.0"
void test_handle_send_ok();               // {"cmd":"send","text":"hi"} → evt "sent", text "hi"; eng.txBusy()==true
void test_handle_send_too_long();         // 65 'a' → evt "error", msg "message too long"; eng.txBusy()==false
void test_handle_send_busy();             // two sends in a row → second is error "busy"
void test_handle_set_threshold();         // value 1200 → eng.threshold()==1200, act.thresholdChanged, reply evt hello threshold 1200
void test_handle_set_threshold_out_of_range(); // 5000 and -1 → error "bad threshold", threshold unchanged
void test_handle_garbage();               // "abc{" → error "bad command"
void test_recv_event_adc_array();         // RxPacket len 2 "hi", adcCount 3 {1,2,3}, ok=false → ok false, adc == [1,2,3]
```

- [ ] **Step 2: Run to verify failure**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: compile errors for `host_io` functions.

- [ ] **Step 3: Implement** `host_io.cpp` with ArduinoJson `JsonDocument`, `deserializeJson`, `serializeJson` into `std::string`.

- [ ] **Step 4: Run to verify pass**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/laserlink test/test_native
git commit -m "feat(fw): JSON host protocol for send/hello/set_threshold"
```

---

### Task 4: Firmware hardware glue (`src/main.cpp`)

**Files:**
- Modify (rewrite): `src/main.cpp`

**Interfaces:**
- Consumes: `LinkEngine`, `handleCommand`, `helloEvent`, `recvEvent`, `HostActions`, `TICKS_PER_BIT`, `BIT_US` (define `BIT_US 1000` in main).

- [ ] **Step 1: Implement** `src/main.cpp`:
  - `setup()`: `Serial.begin(115200)`; `pinMode(LASER_PIN, OUTPUT)` LOW; `analogReadResolution(12)`; `analogSetPinAttenuation(RX_PIN, ADC_11db)`; load threshold from `Preferences` (`"laser"`, `"thr"`, default 2048) into a global `LinkEngine`; print `helloEvent`; `disableCore0WDT()`; start `linkTask` with `xTaskCreatePinnedToCore(..., 0)` at priority `configMAX_PRIORITIES - 1`.
  - `linkTask`: tick period `BIT_US / TICKS_PER_BIT` = 125 µs scheduled against `esp_timer_get_time()` (next += 125, busy-wait until reached; if more than one tick late, resync instead of bursting). Each tick: `digitalWrite(LASER_PIN, eng.tick(analogRead(RX_PIN), millis()))`.
  - Engine shared between the cores is guarded by a `portMUX_TYPE` spinlock around `sendPayload`, `setThreshold` and `takeReceived` only (keep critical sections short; `tick` takes the same lock).
  - `loop()` (core 1): read `Serial` into a line buffer (max 512 chars, drop the line if it overflows, `\r` ignored); on `\n` call `handleCommand`, print the reply + `\n`; if `act.thresholdChanged` save to Preferences. Poll `takeReceived` each loop and print `recvEvent` + `\n`.

- [ ] **Step 2: Build for the board**

Run: `~/.platformio/penv/bin/pio run -e fm-devkit`
Expected: `SUCCESS`.

- [ ] **Step 3: Re-run native tests** (main.cpp must not break the lib)

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(fw): wire link engine to laser/IS203 with core-0 tick task"
```

Hardware check (user, after flashing both boards): in a serial monitor at 115200 on board A send `{"cmd":"send","text":"hi"}`; board B's monitor shows `{"evt":"recv","ok":true,"text":"hi",...}`.

---

### Task 5: App protocol + chat model

**Files:**
- Create: `app/requirements.txt` (`PySide6>=6.8`, `pyserial>=3.5`), `app/requirements-dev.txt` (`-r requirements.txt`, `pytest>=8`), `app/laserchat/__init__.py`, `app/laserchat/protocol.py`, `app/laserchat/model.py`, `app/tests/test_protocol.py`, `app/tests/test_model.py`, `app/tests/conftest.py` (adds `app/` to `sys.path`)

**Interfaces:**
- Produces (`protocol.py`):
  - `MAX_TEXT_BYTES = 64`
  - frozen dataclasses: `Hello(fw: str, bit_us: int, threshold: int)`, `Sent(text: str, bits: str)`, `Recv(ok: bool, text: str, bits: str, adc: list[int])`, `DeviceError(msg: str)`; `Event = Hello | Sent | Recv | DeviceError`
  - `parse_line(line: str | bytes) -> Event | None` — bytes decoded UTF-8 with `errors="replace"`; non-JSON, non-object, unknown `evt` or missing fields → `None`.
  - `class TextTooLong(ValueError)`
  - `utf8_len(text: str) -> int`
  - `cmd_send(text: str) -> bytes` (raises `TextTooLong` if `utf8_len > 64`, `ValueError` if empty), `cmd_hello() -> bytes`, `cmd_set_threshold(value: int) -> bytes` (raises `ValueError` outside 0..4095). All return compact JSON + `b"\n"`.
- Produces (`model.py`):
  - `@dataclass ChatMessage: direction: Literal["out","in"]; text: str; time: datetime; status: Literal["pending","sent","received","corrupted","failed"]; bits: str = ""; adc: list[int] = field(default_factory=list)`
  - `class ChatModel`: `messages: list[ChatMessage]`; `add_pending(text) -> ChatMessage`; `apply(event) -> ChatMessage | None` — `Sent` marks the oldest pending with equal text as `sent` and stores bits; `Recv` appends an `in` message (`received` if ok else `corrupted`); `DeviceError` marks the oldest pending as `failed` and returns it; `Hello` returns None. `fail_all_pending()` for disconnects.

- [ ] **Step 1: Write failing tests**

```python
def test_parse_hello(): assert parse_line('{"evt":"hello","fw":"laser-chat 1.0","bit_us":1000,"threshold":2048}') == Hello("laser-chat 1.0", 1000, 2048)
def test_parse_recv_bytes_with_bad_utf8(): e = parse_line(b'{"evt":"recv","ok":false,"text":"h\xffi","bits":"","adc":[1,2]}'); assert isinstance(e, Recv) and e.ok is False and e.adc == [1, 2]
@pytest.mark.parametrize("line", ["", "garbage", "ets Jun  8 2016 rst:0x1", '{"evt":"recv"', "[1,2]", '{"evt":"nope"}', '{"evt":"sent"}'])
def test_parse_line_ignores_garbage(line): assert parse_line(line) is None
def test_cmd_send_bytes(): assert cmd_send("hi") == b'{"cmd":"send","text":"hi"}\n'
def test_cmd_send_rejects_over_64_utf8_bytes(): cmd_send("a" * 64); pytest.raises(TextTooLong, cmd_send, "😀" * 17)   # 68 bytes
def test_cmd_set_threshold_range(): assert b'"value":4095' in cmd_set_threshold(4095); pytest.raises(ValueError, cmd_set_threshold, 4096)
def test_model_pending_then_sent(): m = ChatModel(); m.add_pending("hi"); msg = m.apply(Sent("hi", "01101000 01101001")); assert msg.status == "sent" and msg.bits.startswith("0110")
def test_model_recv_corrupted(): assert ChatModel().apply(Recv(False, "h?", "", [1])).status == "corrupted"
def test_model_error_fails_pending(): m = ChatModel(); m.add_pending("x"); assert m.apply(DeviceError("busy")).status == "failed"
def test_model_fail_all_pending(): m = ChatModel(); m.add_pending("a"); m.add_pending("b"); m.fail_all_pending(); assert all(x.status == "failed" for x in m.messages)
```

- [ ] **Step 2: Create venv and run to verify failure**

```bash
cd app && python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
.venv/bin/pytest tests -q
```
Expected: `ModuleNotFoundError: laserchat.protocol`.

- [ ] **Step 3: Implement** `protocol.py` and `model.py` per Interfaces (`json.dumps(..., separators=(",", ":"), ensure_ascii=False)`).

- [ ] **Step 4: Run to verify pass**

Run: `cd app && .venv/bin/pytest tests -q`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(app): serial JSON protocol and chat model"
```

---

### Task 6: Serial worker, widgets, main window

**Files:**
- Create: `app/laserchat/serial_worker.py`, `app/laserchat/signal_view.py`, `app/laserchat/chat_view.py`, `app/laserchat/main_window.py`, `app/laser_chat.py`, `app/tests/test_widgets.py`, `app/tests/test_window.py`
- Modify: `app/tests/conftest.py` (set `QT_QPA_PLATFORM=offscreen` before importing Qt; session-scoped `QApplication` fixture `qapp`)

**Interfaces:**
- Consumes: everything from Task 5.
- Produces:
  - `list_ports() -> list[tuple[str, str]]` — (device, description) from `serial.tools.list_ports.comports()`, sorted by device.
  - `class SerialWorker(QThread)`: `__init__(self, port: str, baud: int = 115200, opener: Callable[..., Any] = serial.Serial)`; signals `line_received = Signal(bytes)`, `disconnected = Signal(str)`; `write(data: bytes) -> None` (thread-safe, a failure emits `disconnected`); `stop() -> None`. `run()` opens with `timeout=0.1`, reads with `readline()`, emits non-empty lines; any `serial.SerialException`/`OSError` → emit `disconnected(str(exc))` and exit. `opener` is injectable for tests.
  - `class SignalView(QWidget)`: `set_measured(adc: list[int], threshold: int) -> None`, `set_drawn(bits: str) -> None` (spaces ignored; adds start/stop per 8 bits), `clear() -> None`; properties `mode: Literal["empty","measured","drawn"]`, `caption: str` (`"measured · N samples"` / `"sent (drawn)"`). `paintEvent` draws a step trace on 0–4095 (y inverted so low ADC = "light" is drawn high, matching laser ON), a dashed threshold line in measured mode, and alternating shading every 10 samples (one frame).
  - `class ChatView(QScrollArea)`: `add_message(msg: ChatMessage) -> None`, `update_message(msg: ChatMessage) -> None` (finds the bubble by object identity), signal `message_clicked = Signal(object)`; auto-scrolls to bottom. Bubble: out = right, red `#e0483e`; in = left, `#262a34`; corrupted = amber border + label "⚠ corrupted (CRC failed)"; meta line `HH:MM · <status>`; sent bubbles show `bits` in a small monospace line.
  - `class MainWindow(QMainWindow)`: `__init__(self, worker_factory: Callable[[str], SerialWorker] = SerialWorker)`. Top bar: port `QComboBox` + refresh button, Connect/Disconnect button, status dot (`set_status("disconnected"|"waiting"|"online")` — red/amber/green), threshold `QSpinBox` 0–4095 + Apply (sends `cmd_set_threshold`). Chat, then `SignalView`, then input `QLineEdit` + byte counter label `"n/64"` (red and Send disabled when > 64 bytes or empty) + Send; Enter sends. Connect → status `waiting`, sends `cmd_hello()`; `Hello` → `online`, spin box set to its threshold. `Recv` → new bubble and `SignalView.set_measured`. Clicking an in-bubble → measured; an out-bubble → `set_drawn(bits)`. `disconnected` → status red, `fail_all_pending`, input disabled, button back to "Connect". `handle_line(line: bytes)` is the public slot connected to `line_received` (tests call it directly).
  - `apply_dark_theme(app: QApplication) -> None` — Fusion style + dark `QPalette`.
  - `app/laser_chat.py`: `main()` creates the QApplication, applies the theme, shows `MainWindow`, title "Laser Chat".

- [ ] **Step 1: Write failing tests**

```python
# test_widgets.py
def test_signal_view_measured(qapp): v = SignalView(); v.set_measured([300, 3900] * 10, 2048); assert v.mode == "measured" and "20 samples" in v.caption; v.grab()   # paints without error
def test_signal_view_drawn(qapp): v = SignalView(); v.set_drawn("01101000"); assert v.mode == "drawn" and v.caption == "sent (drawn)"; v.grab()
def test_signal_view_empty_paints(qapp): SignalView().grab()
def test_worker_emits_disconnected_on_serial_error(qapp):
    class Boom:
        def __init__(self, *a, **k): pass
        def readline(self): raise serial.SerialException("device reports readiness to read but returned no data")
        def close(self): pass
    w = SerialWorker("X", opener=Boom); got = []; w.disconnected.connect(got.append); w.start(); w.wait(2000); qapp.processEvents(); assert got

# test_window.py — FakeWorker: QObject with the same signals, records write() calls, start/stop no-ops
def test_window_connect_sends_hello(qapp): ...          # select port "FAKE", click connect → fake.writes[0] == cmd_hello(); status == "waiting"
def test_window_hello_goes_online(qapp): ...            # handle_line(hello threshold 1700) → status "online", spin.value()==1700
def test_window_send_and_sent_echo(qapp): ...           # type "hi", press Enter → writes contains cmd_send("hi"); bubble pending; handle_line(sent) → status "sent"
def test_window_recv_updates_signal(qapp): ...          # handle_line(recv ok adc [300]*80) → new in-bubble; signal_view.mode == "measured"
def test_window_counter_blocks_over_64_bytes(qapp): ... # input "😀"*17 → counter text "68/64", send button disabled
def test_window_disconnect_disables_input(qapp): ...    # fake.disconnected.emit("gone") → status "disconnected", input disabled, pending → failed
```

- [ ] **Step 2: Run to verify failure**

Run: `cd app && .venv/bin/pytest tests -q`
Expected: import errors for new modules.

- [ ] **Step 3: Implement** the four modules + `laser_chat.py` per Interfaces. Expose `status`, `port_combo`, `connect_button`, `threshold_spin`, `input_edit`, `send_button`, `counter_label`, `chat_view`, `signal_view` as attributes so the tests can reach them.

- [ ] **Step 4: Run to verify pass**

Run: `cd app && .venv/bin/pytest tests -q`
Expected: all PASS.

- [ ] **Step 5: Manual smoke** — `cd app && .venv/bin/python laser_chat.py` opens the dark window with an empty port list or real ports; close it. (If no display, skip and note it.)

- [ ] **Step 6: Commit**

```bash
git add app
git commit -m "feat(app): PySide6 chat window with measured signal view"
```

---

### Task 7: README and run instructions

**Files:**
- Create: `app/README.md`
- Create: `README.md` (repo root; short: what it is, flash firmware, run app, link to app README)

- [ ] **Step 1: Write** `app/README.md` with:
  - Setup for each OS:
    - Fedora: `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt`, `sudo usermod -aG dialout $USER` then log out and back in.
    - macOS: same venv commands; CH340/CP210x driver note; ports look like `/dev/cu.usbserial-*`.
    - Windows: `py -m venv .venv`, `.venv\Scripts\pip install -r requirements.txt`, then `.venv\Scripts\python laser_chat.py`; find the COM port in Device Manager.
  - Using it: pick port → Connect → status turns green → type a message. Threshold box: lower it if received messages are corrupted while the laser is clearly reaching the sensor; check the signal strip, where light-ON samples should be well below the dashed line.
  - Optional PyInstaller one-file build: `pip install pyinstaller && pyinstaller --onefile --windowed laser_chat.py` (build on each OS).
  - Root README: flash with `~/.platformio/penv/bin/pio run -e fm-devkit -t upload` (or the VS Code PlatformIO Upload button) on **both** boards; run tests with `pio test -e native` and `cd app && .venv/bin/pytest`.

- [ ] **Step 2: Verify** every command in the README exists in this plan/repo (`pio` envs `fm-devkit`/`native`, `app/laser_chat.py`, `requirements.txt`).

- [ ] **Step 3: Commit**

```bash
git add README.md app/README.md
git commit -m "docs: setup and usage for firmware and desktop app"
```
