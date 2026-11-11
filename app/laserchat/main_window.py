"""Main window: port picker, chat, signal strip, message box."""
from __future__ import annotations

from typing import Any, Callable

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import (
    QApplication, QComboBox, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QPushButton,
    QSpinBox, QStyleFactory, QToolButton, QVBoxLayout, QWidget,
)

from .chat_view import ChatView
from .model import ChatMessage, ChatModel
from .protocol import (
    MAX_TEXT_BYTES, DeviceError, Hello, Recv, Sent,
    cmd_hello, cmd_send, cmd_set_threshold, parse_line, utf8_len,
)
from .serial_worker import SerialWorker, list_ports
from .signal_view import SignalView

STATUS_COLORS = {"disconnected": "#e5484d", "waiting": "#f0a33a", "online": "#3ddc84"}
STATUS_TEXT = {"disconnected": "Disconnected", "waiting": "Waiting for board…", "online": "Online"}


def apply_dark_theme(app: QApplication) -> None:
    app.setStyle(QStyleFactory.create("Fusion"))
    pal = QPalette()
    base, panel, text = QColor("#14161b"), QColor("#1b1e25"), QColor("#e6e8ee")
    pal.setColor(QPalette.Window, base)
    pal.setColor(QPalette.WindowText, text)
    pal.setColor(QPalette.Base, QColor("#232733"))
    pal.setColor(QPalette.AlternateBase, panel)
    pal.setColor(QPalette.Text, text)
    pal.setColor(QPalette.Button, panel)
    pal.setColor(QPalette.ButtonText, text)
    pal.setColor(QPalette.Highlight, QColor("#e0483e"))
    pal.setColor(QPalette.HighlightedText, QColor("#ffffff"))
    pal.setColor(QPalette.ToolTipBase, panel)
    pal.setColor(QPalette.ToolTipText, text)
    pal.setColor(QPalette.PlaceholderText, QColor("#7d8394"))
    for role in (QPalette.Text, QPalette.ButtonText, QPalette.WindowText):
        pal.setColor(QPalette.Disabled, role, QColor("#5a5f6c"))
    app.setPalette(pal)


class MainWindow(QMainWindow):
    def __init__(self,
                 worker_factory: Callable[[str], Any] = SerialWorker,
                 ports_provider: Callable[[], list[tuple[str, str]]] = list_ports) -> None:
        super().__init__()
        self.setWindowTitle("Laser Chat")
        self.resize(760, 640)
        self._worker_factory = worker_factory
        self._ports_provider = ports_provider
        self._worker: Any = None
        self._threshold = 2048
        self.model = ChatModel()
        self.status = "disconnected"
        # The board refuses a send while it is still transmitting the previous one;
        # retry instead of failing the message.
        self.busy_retry_ms = 300
        self.busy_max_retries = 10
        self._retries: dict[int, int] = {}

        central = QWidget()
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)
        self.setCentralWidget(central)

        # --- top bar ---
        bar = QWidget()
        bar.setObjectName("topbar")
        bar.setStyleSheet("#topbar { background: #1b1e25; border-bottom: 1px solid #2a2e37; }")
        top = QHBoxLayout(bar)
        top.setContentsMargins(12, 8, 12, 8)
        self.status_dot = QLabel()
        self.status_dot.setFixedSize(10, 10)
        self.status_label = QLabel()
        title = QLabel("<b>Laser Chat</b>")
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(200)
        self.refresh_button = QToolButton()
        self.refresh_button.setText("⟳")
        self.refresh_button.setToolTip("Refresh port list")
        self.connect_button = QPushButton("Connect")
        self.threshold_spin = QSpinBox()
        self.threshold_spin.setRange(0, 4095)
        self.threshold_spin.setValue(self._threshold)
        self.threshold_spin.setToolTip("Sensor readings below this count as laser ON")
        self.threshold_apply = QPushButton("Apply")
        top.addWidget(self.status_dot)
        top.addWidget(title)
        top.addWidget(self.status_label)
        top.addStretch(1)
        top.addWidget(QLabel("Threshold"))
        top.addWidget(self.threshold_spin)
        top.addWidget(self.threshold_apply)
        top.addSpacing(12)
        top.addWidget(self.port_combo)
        top.addWidget(self.refresh_button)
        top.addWidget(self.connect_button)
        root.addWidget(bar)

        # --- chat + signal ---
        self.chat_view = ChatView()
        root.addWidget(self.chat_view, 1)
        self.signal_view = SignalView()
        sig_wrap = QWidget()
        sl = QVBoxLayout(sig_wrap)
        sl.setContentsMargins(12, 6, 12, 6)
        sl.addWidget(self.signal_view)
        root.addWidget(sig_wrap)

        # --- input row ---
        inp = QWidget()
        inp.setObjectName("inputrow")
        inp.setStyleSheet("#inputrow { border-top: 1px solid #2a2e37; }")
        il = QHBoxLayout(inp)
        il.setContentsMargins(12, 8, 12, 10)
        self.input_edit = QLineEdit()
        self.input_edit.setPlaceholderText("Type a message…")
        self.counter_label = QLabel(f"0/{MAX_TEXT_BYTES}")
        self.counter_label.setMinimumWidth(48)
        self.send_button = QPushButton("Send")
        self.send_button.setStyleSheet(
            "QPushButton { background: #e0483e; color: white; font-weight: 600; padding: 6px 16px;"
            " border-radius: 6px; } QPushButton:disabled { background: #4a2a2a; color: #a08080; }")
        il.addWidget(self.input_edit, 1)
        il.addWidget(self.counter_label)
        il.addWidget(self.send_button)
        root.addWidget(inp)

        # --- wiring ---
        self.refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button.clicked.connect(self._toggle_connection)
        self.threshold_apply.clicked.connect(self._apply_threshold)
        self.input_edit.textChanged.connect(self._update_input_state)
        self.input_edit.returnPressed.connect(self._send)
        self.send_button.clicked.connect(self._send)
        self.chat_view.message_clicked.connect(self._show_signal)

        self.refresh_ports()
        self.set_status("disconnected")

    # ---- connection ----

    def refresh_ports(self) -> None:
        current = self.port_combo.currentData()
        self.port_combo.clear()
        for device, desc in self._ports_provider():
            label = device if desc in ("", device, "n/a") else f"{device} — {desc}"
            self.port_combo.addItem(label, device)
        idx = self.port_combo.findData(current)
        if idx >= 0:
            self.port_combo.setCurrentIndex(idx)
        self._update_input_state()

    def set_status(self, status: str) -> None:
        self.status = status
        self.status_dot.setStyleSheet(
            f"background: {STATUS_COLORS[status]}; border-radius: 5px;")
        self.status_label.setText(STATUS_TEXT[status])
        connected = status != "disconnected"
        self.connect_button.setText("Disconnect" if connected else "Connect")
        self.port_combo.setEnabled(not connected)
        self.refresh_button.setEnabled(not connected)
        self._update_input_state()

    def _toggle_connection(self) -> None:
        if self._worker is not None:
            self._close_worker()
            self.model.fail_all_pending()
            self._refresh_all_bubbles()
            self.set_status("disconnected")
            return
        port = self.port_combo.currentData()
        if not port:
            self.refresh_ports()
            return
        w = self._worker_factory(port)
        w.line_received.connect(self.handle_line)
        w.disconnected.connect(self._on_disconnected)
        self._worker = w
        w.start()
        self.set_status("waiting")
        w.write(cmd_hello())

    def _close_worker(self) -> None:
        w, self._worker = self._worker, None
        if w is not None:
            try:
                w.line_received.disconnect(self.handle_line)
                w.disconnected.disconnect(self._on_disconnected)
            except (RuntimeError, TypeError):
                pass
            w.stop()

    def _on_disconnected(self, reason: str) -> None:
        if self.sender() is not None and self.sender() is not self._worker:
            return
        self._close_worker()
        self.model.fail_all_pending()
        self._refresh_all_bubbles()
        self.set_status("disconnected")
        self.status_label.setText(f"Disconnected: {reason}" if reason else "Disconnected")

    # ---- incoming ----

    def handle_line(self, line: bytes) -> None:
        event = parse_line(line)
        if event is None:
            return
        if isinstance(event, Hello):
            self._threshold = event.threshold
            self.threshold_spin.setValue(event.threshold)
            self.set_status("online")
            return
        if isinstance(event, DeviceError) and event.msg == "busy" and self._retry_busy():
            return
        msg = self.model.apply(event)
        if msg is None:
            if isinstance(event, DeviceError):
                self.statusBar().showMessage(f"Board: {event.msg}", 5000)
            return
        if isinstance(event, Recv):
            self.chat_view.add_message(msg)
            self._show_signal(msg)
        elif isinstance(event, (Sent, DeviceError)):
            self.chat_view.update_message(msg)
            if isinstance(event, DeviceError):
                self.statusBar().showMessage(f"Board: {event.msg}", 5000)

    # ---- outgoing ----

    def _update_input_state(self, *_: object) -> None:
        online = self.status == "online"
        text = self.input_edit.text()
        n = utf8_len(text)
        too_long = n > MAX_TEXT_BYTES
        self.counter_label.setText(f"{n}/{MAX_TEXT_BYTES}")
        self.counter_label.setStyleSheet("color: #e5484d;" if too_long else "color: #7d8394;")
        self.input_edit.setEnabled(online)
        self.send_button.setEnabled(online and n > 0 and not too_long)
        self.threshold_apply.setEnabled(online)

    def _send(self) -> None:
        if not self.send_button.isEnabled() or self._worker is None:
            return
        text = self.input_edit.text()
        try:
            data = cmd_send(text)
        except ValueError:
            return
        msg = self.model.add_pending(text)
        self.chat_view.add_message(msg)
        self.input_edit.clear()
        self._worker.write(data)

    def _retry_busy(self) -> bool:
        msg = next((m for m in self.model.messages if m.status == "pending"), None)
        if msg is None:
            return False
        tries = self._retries.get(id(msg), 0)
        if tries >= self.busy_max_retries:
            self._retries.pop(id(msg), None)
            return False
        self._retries[id(msg)] = tries + 1
        QTimer.singleShot(self.busy_retry_ms, self, lambda m=msg: self._resend(m))
        return True

    def _resend(self, msg: ChatMessage) -> None:
        if msg.status == "pending" and self._worker is not None:
            try:
                self._worker.write(cmd_send(msg.text))
            except ValueError:
                pass

    def _apply_threshold(self) -> None:
        if self._worker is not None:
            self._worker.write(cmd_set_threshold(self.threshold_spin.value()))

    # ---- signal strip ----

    def _show_signal(self, msg: ChatMessage) -> None:
        if msg.direction == "in":
            self.signal_view.set_measured(msg.adc, self._threshold)
        elif msg.bits:
            self.signal_view.set_drawn(msg.bits)

    def _refresh_all_bubbles(self) -> None:
        for m in self.model.messages:
            self.chat_view.update_message(m)

    def closeEvent(self, event) -> None:
        self._close_worker()
        super().closeEvent(event)
