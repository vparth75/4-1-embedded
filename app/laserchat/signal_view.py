"""Strip chart of the laser signal: measured ADC readings or drawn bits."""
from __future__ import annotations

from typing import Literal

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QSizePolicy, QWidget

ADC_MAX = 4095
FRAME = 10          # start + 8 data + stop

BG = QColor("#0f1115")
TRACE = QColor("#e0483e")
SHADE = QColor(255, 255, 255, 10)
THRESH = QColor("#f0a33a")
TEXT = QColor("#8a90a0")


class SignalView(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumHeight(90)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.mode: Literal["empty", "measured", "drawn"] = "empty"
        self.caption = "no signal yet: received messages show the measured sensor readings"
        self._values: list[float] = []      # 0..1, 1 = laser ON
        self._threshold: float | None = None

    def clear(self) -> None:
        self.mode = "empty"
        self.caption = "no signal yet"
        self._values = []
        self._threshold = None
        self.update()

    def set_measured(self, adc: list[int], threshold: int) -> None:
        # Light pulls the reading low, so invert: low ADC is drawn high (laser ON).
        self.mode = "measured"
        self.caption = f"measured · {len(adc)} samples · threshold {threshold}"
        self._values = [1.0 - min(max(v, 0), ADC_MAX) / ADC_MAX for v in adc]
        self._threshold = 1.0 - threshold / ADC_MAX
        self.update()

    def set_drawn(self, bits: str) -> None:
        self.mode = "drawn"
        self.caption = "sent (drawn)"
        clean = [c for c in bits if c in "01"]
        values: list[float] = []
        for i in range(0, len(clean) - len(clean) % 8, 8):
            values.append(1.0)                                   # start bit
            values.extend(1.0 if c == "1" else 0.0 for c in clean[i:i + 8])
            values.append(0.0)                                   # stop bit
        self._values = values
        self._threshold = None
        self.update()

    def paintEvent(self, _event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect())
        p.fillRect(r, BG)

        top, bottom = 22.0, r.height() - 8.0
        left, right = 8.0, r.width() - 8.0
        plot_h = max(bottom - top, 1.0)

        p.setPen(TEXT)
        font = QFont(self.font())
        font.setPointSizeF(max(font.pointSizeF() - 1, 7))
        p.setFont(font)
        p.drawText(QRectF(left, 2, right - left, 18), Qt.AlignLeft | Qt.AlignVCenter,
                   "Laser signal · " + self.caption)

        n = len(self._values)
        if n == 0:
            p.end()
            return

        step = (right - left) / n

        # Shade every other frame so bytes are easy to tell apart.
        for f in range(0, n, FRAME * 2):
            p.fillRect(QRectF(left + f * step, top, FRAME * step, plot_h), SHADE)

        def y(v: float) -> float:
            return bottom - v * plot_h

        if self._threshold is not None:
            pen = QPen(THRESH, 1, Qt.DashLine)
            p.setPen(pen)
            ty = y(self._threshold)
            p.drawLine(QPointF(left, ty), QPointF(right, ty))

        path = QPainterPath(QPointF(left, y(self._values[0])))
        for i, v in enumerate(self._values):
            x0 = left + i * step
            path.lineTo(QPointF(x0, y(v)))
            path.lineTo(QPointF(x0 + step, y(v)))
        p.setPen(QPen(TRACE, 2))
        p.drawPath(path)
        p.end()
