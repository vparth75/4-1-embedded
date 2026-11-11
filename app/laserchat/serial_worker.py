"""Background thread that owns the serial port."""
from __future__ import annotations

import threading
from typing import Any, Callable

import serial
import serial.tools.list_ports
from PySide6.QtCore import QThread, Signal


def list_ports() -> list[tuple[str, str]]:
    """(device, description) for every serial port, sorted by device name."""
    ports = [(p.device, p.description or p.device) for p in serial.tools.list_ports.comports()]
    return sorted(ports)


class SerialWorker(QThread):
    line_received = Signal(bytes)
    disconnected = Signal(str)

    def __init__(self, port: str, baud: int = 115200,
                 opener: Callable[..., Any] = serial.Serial) -> None:
        super().__init__()
        self.port = port
        self._baud = baud
        self._opener = opener
        self._ser: Any = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._reported = False
        self._backlog: list[bytes] = []   # writes made before the port finished opening

    def _report(self, reason: str) -> None:
        # Emit at most once, and not after a user-requested stop.
        if not self._reported and not self._stop.is_set():
            self._reported = True
            self.disconnected.emit(reason)

    def run(self) -> None:
        try:
            ser = self._opener(self.port, self._baud, timeout=0.1)
        except (serial.SerialException, OSError, ValueError) as exc:
            self._report(str(exc))
            return
        try:
            with self._lock:
                self._ser = ser
                backlog, self._backlog = self._backlog, []
                for data in backlog:
                    ser.write(data)
            while not self._stop.is_set():
                line = ser.readline()
                if line:
                    self.line_received.emit(bytes(line))
        except (serial.SerialException, OSError, TypeError) as exc:
            self._report(str(exc) or "serial port error")
        finally:
            with self._lock:
                self._ser = None
            try:
                ser.close()
            except Exception:
                pass

    def write(self, data: bytes) -> None:
        with self._lock:
            ser = self._ser
            if ser is None:
                if not self._stop.is_set():
                    self._backlog.append(data)
                return
            try:
                ser.write(data)
            except (serial.SerialException, OSError) as exc:
                failure = str(exc) or "write failed"
            else:
                return
        self._report(failure)

    def stop(self) -> None:
        self._stop.set()
        self.wait(1000)
