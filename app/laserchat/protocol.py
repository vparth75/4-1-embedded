"""JSON-lines protocol between the app and the ESP32 firmware."""
from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Union

MAX_TEXT_BYTES = 64


@dataclass(frozen=True)
class Hello:
    fw: str
    bit_us: int
    threshold: int


@dataclass(frozen=True)
class Sent:
    text: str
    bits: str


@dataclass(frozen=True)
class Recv:
    ok: bool
    text: str
    bits: str
    adc: list[int]


@dataclass(frozen=True)
class DeviceError:
    msg: str


Event = Union[Hello, Sent, Recv, DeviceError]


class TextTooLong(ValueError):
    pass


def utf8_len(text: str) -> int:
    return len(text.encode("utf-8"))


def _is_int(v: object) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def parse_line(line: str | bytes) -> Event | None:
    """Turn one serial line into an Event, or None if it isn't one we understand."""
    if isinstance(line, bytes):
        line = line.decode("utf-8", errors="replace")
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        # strict=False: firmware leaves most control bytes unescaped (e.g. corrupted text).
        d = json.loads(line, strict=False)
    except ValueError:
        return None
    if not isinstance(d, dict):
        return None

    evt = d.get("evt")
    if evt == "hello":
        fw, bit_us, thr = d.get("fw"), d.get("bit_us"), d.get("threshold")
        if isinstance(fw, str) and _is_int(bit_us) and _is_int(thr):
            return Hello(fw, bit_us, thr)
    elif evt == "sent":
        text, bits = d.get("text"), d.get("bits")
        if isinstance(text, str) and isinstance(bits, str):
            return Sent(text, bits)
    elif evt == "recv":
        ok, text, bits, adc = d.get("ok"), d.get("text"), d.get("bits"), d.get("adc")
        if (isinstance(ok, bool) and isinstance(text, str) and isinstance(bits, str)
                and isinstance(adc, list) and all(_is_int(x) for x in adc)):
            return Recv(ok, text, bits, adc)
    elif evt == "error":
        msg = d.get("msg")
        if isinstance(msg, str):
            return DeviceError(msg)
    return None


def _encode(obj: dict) -> bytes:
    return (json.dumps(obj, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def cmd_send(text: str) -> bytes:
    if not text:
        raise ValueError("empty message")
    if utf8_len(text) > MAX_TEXT_BYTES:
        raise TextTooLong(f"message is {utf8_len(text)} bytes, limit is {MAX_TEXT_BYTES}")
    return _encode({"cmd": "send", "text": text})


def cmd_hello() -> bytes:
    return _encode({"cmd": "hello"})


def cmd_set_threshold(value: int) -> bytes:
    if not 0 <= value <= 4095:
        raise ValueError("threshold must be 0..4095")
    return _encode({"cmd": "set_threshold", "value": int(value)})
