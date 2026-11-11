"""Scrollable list of chat bubbles."""
from __future__ import annotations

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QScrollArea, QVBoxLayout, QWidget

from .model import ChatMessage

OUT_BG = "#e0483e"
IN_BG = "#262a34"
WARN = "#f0a33a"

STATUS_TEXT = {
    "pending": "sending…",
    "sent": "sent ✓",
    "received": "received",
    "corrupted": "⚠ corrupted (CRC failed)",
    "failed": "✗ not sent",
}


class Bubble(QFrame):
    clicked = Signal(object)

    def __init__(self, msg: ChatMessage) -> None:
        super().__init__()
        self.msg = msg
        self.setCursor(Qt.PointingHandCursor)
        self.setMaximumWidth(520)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 8, 12, 8)
        lay.setSpacing(2)
        self.text_label = QLabel()
        self.text_label.setWordWrap(True)
        self.text_label.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.bits_label = QLabel()
        self.bits_label.setWordWrap(True)
        self.bits_label.setStyleSheet("font-family: monospace; font-size: 10px;")
        self.meta_label = QLabel()
        self.meta_label.setStyleSheet("font-size: 10px;")
        lay.addWidget(self.text_label)
        lay.addWidget(self.bits_label)
        lay.addWidget(self.meta_label)
        self.refresh()

    def refresh(self) -> None:
        m = self.msg
        self.text_label.setText(m.text)
        # Word-wrapped labels shrink to their narrowest; keep short messages on one line.
        fm = self.text_label.fontMetrics()
        self.text_label.setMinimumWidth(min(fm.horizontalAdvance(m.text) + 6, 480))
        self.bits_label.setText(m.bits if m.direction == "out" else "")
        self.bits_label.setVisible(bool(m.bits) and m.direction == "out")
        self.meta_label.setText(f"{m.time:%H:%M} · {STATUS_TEXT.get(m.status, m.status)}")

        bg = OUT_BG if m.direction == "out" else IN_BG
        border = f"2px solid {WARN}" if m.status in ("corrupted", "failed") else "none"
        self.setStyleSheet(
            f"Bubble {{ background: {bg}; border: {border}; border-radius: 12px; }}"
            f"QLabel {{ color: #f2f3f7; background: transparent; }}"
        )
        meta_color = WARN if m.status in ("corrupted", "failed") else "rgba(242,243,247,0.65)"
        self.meta_label.setStyleSheet(f"font-size: 10px; color: {meta_color};")

    def mousePressEvent(self, event) -> None:
        self.clicked.emit(self.msg)
        super().mousePressEvent(event)


class ChatView(QScrollArea):
    message_clicked = Signal(object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWidgetResizable(True)
        self.setFrameShape(QFrame.NoFrame)
        self._inner = QWidget()
        self._layout = QVBoxLayout(self._inner)
        self._layout.setContentsMargins(14, 14, 14, 14)
        self._layout.setSpacing(8)
        self._layout.addStretch(1)
        self.setWidget(self._inner)
        self._bubbles: dict[int, Bubble] = {}

    def add_message(self, msg: ChatMessage) -> None:
        bubble = Bubble(msg)
        bubble.clicked.connect(self.message_clicked)
        row = QHBoxLayout()
        if msg.direction == "out":
            row.addStretch(1)
            row.addWidget(bubble)
        else:
            row.addWidget(bubble)
            row.addStretch(1)
        self._layout.addLayout(row)
        self._bubbles[id(msg)] = bubble
        QTimer.singleShot(0, self, self._scroll_to_bottom)

    def update_message(self, msg: ChatMessage) -> None:
        bubble = self._bubbles.get(id(msg))
        if bubble is not None and bubble.msg is msg:
            bubble.refresh()

    def _scroll_to_bottom(self) -> None:
        bar = self.verticalScrollBar()
        bar.setValue(bar.maximum())
