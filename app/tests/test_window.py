import json

import pytest
from PySide6.QtCore import QObject, Qt, Signal
from PySide6.QtTest import QTest

from laserchat.main_window import MainWindow
from laserchat.protocol import cmd_hello, cmd_send, cmd_set_threshold


class FakeWorker(QObject):
    line_received = Signal(bytes)
    disconnected = Signal(str)

    def __init__(self, port):
        super().__init__()
        self.port = port
        self.writes = []
        self.started = False
        self.stopped = False

    def start(self):
        self.started = True

    def stop(self):
        self.stopped = True

    def write(self, data):
        self.writes.append(data)


def line(obj):
    return (json.dumps(obj) + "\n").encode()


@pytest.fixture
def win(qapp):
    workers = []

    def factory(port):
        w = FakeWorker(port)
        workers.append(w)
        return w

    w = MainWindow(worker_factory=factory, ports_provider=lambda: [("FAKE", "Fake ESP32")])
    w.workers = workers
    yield w
    w.close()


def connect(win):
    win.port_combo.setCurrentIndex(0)
    QTest.mouseClick(win.connect_button, Qt.LeftButton)
    return win.workers[-1]


def go_online(win, threshold=2048):
    fake = connect(win)
    win.handle_line(line({"evt": "hello", "fw": "laser-chat 1.0", "bit_us": 1000, "threshold": threshold}))
    return fake


def test_window_starts_disconnected(win):
    assert win.status == "disconnected"
    assert not win.input_edit.isEnabled()
    assert win.port_combo.count() == 1


def test_window_connect_sends_hello(win):
    fake = connect(win)
    assert fake.port == "FAKE" and fake.started
    assert fake.writes[0] == cmd_hello()
    assert win.status == "waiting"


def test_window_hello_goes_online(win):
    go_online(win, threshold=1700)
    assert win.status == "online"
    assert win.threshold_spin.value() == 1700
    assert win.input_edit.isEnabled()


def test_window_send_and_sent_echo(win):
    fake = go_online(win)
    win.input_edit.setText("hi")
    QTest.keyClick(win.input_edit, Qt.Key_Return)
    assert cmd_send("hi") in fake.writes
    assert win.input_edit.text() == ""
    msg = win.model.messages[-1]
    assert msg.status == "pending"
    win.handle_line(line({"evt": "sent", "text": "hi", "bits": "01101000 01101001"}))
    assert msg.status == "sent"


def test_window_recv_updates_signal(win):
    go_online(win)
    win.handle_line(line({"evt": "recv", "ok": True, "text": "yo", "bits": "", "adc": [300] * 80}))
    assert win.model.messages[-1].direction == "in"
    assert win.signal_view.mode == "measured"


def test_window_click_bubbles_switch_signal(win):
    go_online(win)
    win.handle_line(line({"evt": "recv", "ok": True, "text": "yo", "bits": "", "adc": [300] * 80}))
    win.input_edit.setText("hi")
    win.send_button.click()
    win.handle_line(line({"evt": "sent", "text": "hi", "bits": "01101000 01101001"}))
    out_msg, in_msg = win.model.messages[1], win.model.messages[0]
    win.chat_view.message_clicked.emit(out_msg)
    assert win.signal_view.mode == "drawn"
    win.chat_view.message_clicked.emit(in_msg)
    assert win.signal_view.mode == "measured"


def test_window_corrupted_message_marked(win):
    go_online(win)
    win.handle_line(line({"evt": "recv", "ok": False, "text": "h?", "bits": "", "adc": [1]}))
    assert win.model.messages[-1].status == "corrupted"


def test_window_counter_blocks_over_64_bytes(win):
    go_online(win)
    win.input_edit.setText("😀" * 17)
    assert win.counter_label.text() == "68/64"
    assert not win.send_button.isEnabled()
    win.input_edit.setText("ok")
    assert win.counter_label.text() == "2/64"
    assert win.send_button.isEnabled()


def test_window_empty_send_disabled(win):
    go_online(win)
    win.input_edit.setText("")
    assert not win.send_button.isEnabled()


def test_window_threshold_apply(win):
    fake = go_online(win)
    win.threshold_spin.setValue(1500)
    win.threshold_apply.click()
    assert cmd_set_threshold(1500) in fake.writes


def test_window_ignores_garbage_lines(win):
    go_online(win)
    win.handle_line(b"ets Jun  8 2016 rst:0x1\n")
    win.handle_line(b"\xff\xfe\n")
    assert win.model.messages == []
    assert win.status == "online"


def test_window_disconnect_disables_input(win):
    fake = go_online(win)
    win.input_edit.setText("x")
    win.send_button.click()
    fake.disconnected.emit("gone")
    assert win.status == "disconnected"
    assert not win.input_edit.isEnabled()
    assert win.connect_button.text() == "Connect"
    assert win.model.messages[-1].status == "failed"


def test_window_reconnect_after_disconnect(win):
    fake = go_online(win)
    fake.disconnected.emit("gone")
    fake2 = connect(win)
    assert fake2 is not fake
    assert win.status == "waiting"


def test_window_user_disconnect_stops_worker(win):
    fake = go_online(win)
    QTest.mouseClick(win.connect_button, Qt.LeftButton)
    assert fake.stopped
    assert win.status == "disconnected"
