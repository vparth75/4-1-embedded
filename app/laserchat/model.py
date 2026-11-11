"""Chat state: what the window shows, independent of Qt."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from typing import Literal

from .protocol import DeviceError, Event, Recv, Sent

Status = Literal["pending", "sent", "received", "corrupted", "failed"]


@dataclass(eq=False)
class ChatMessage:
    direction: Literal["out", "in"]
    text: str
    time: datetime
    status: Status
    bits: str = ""
    adc: list[int] = field(default_factory=list)


class ChatModel:
    def __init__(self) -> None:
        self.messages: list[ChatMessage] = []

    def _oldest_pending(self, text: str | None = None) -> ChatMessage | None:
        for m in self.messages:
            if m.status == "pending" and (text is None or m.text == text):
                return m
        return None

    def add_pending(self, text: str) -> ChatMessage:
        msg = ChatMessage("out", text, datetime.now(), "pending")
        self.messages.append(msg)
        return msg

    def apply(self, event: Event) -> ChatMessage | None:
        """Update state from a device event. Returns the message that changed or was added."""
        if isinstance(event, Sent):
            msg = self._oldest_pending(event.text) or self._oldest_pending()
            if msg is not None:
                msg.status = "sent"
                msg.bits = event.bits
            return msg
        if isinstance(event, Recv):
            msg = ChatMessage("in", event.text, datetime.now(),
                              "received" if event.ok else "corrupted",
                              bits=event.bits, adc=list(event.adc))
            self.messages.append(msg)
            return msg
        if isinstance(event, DeviceError):
            msg = self._oldest_pending()
            if msg is not None:
                msg.status = "failed"
            return msg
        return None

    def fail_all_pending(self) -> None:
        for m in self.messages:
            if m.status == "pending":
                m.status = "failed"
