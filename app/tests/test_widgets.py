import serial

from laserchat.serial_worker import SerialWorker
from laserchat.signal_view import SignalView


def test_signal_view_measured(qapp):
    v = SignalView()
    v.resize(400, 120)
    v.set_measured([300, 3900] * 10, 2048)
    assert v.mode == "measured"
    assert "20 samples" in v.caption
    v.grab()


def test_signal_view_drawn(qapp):
    v = SignalView()
    v.resize(400, 120)
    v.set_drawn("01101000 01101001")
    assert v.mode == "drawn"
    assert v.caption == "sent (drawn)"
    v.grab()


def test_signal_view_empty_paints(qapp):
    v = SignalView()
    v.resize(400, 120)
    assert v.mode == "empty"
    v.grab()
    v.set_measured([], 2048)   # no samples must not crash painting
    v.grab()


class _Boom:
    def __init__(self, *a, **k):
        pass

    def readline(self):
        raise serial.SerialException("device reports readiness to read but returned no data")

    def write(self, data):
        pass

    def close(self):
        pass


def test_worker_emits_disconnected_on_serial_error(qapp):
    w = SerialWorker("X", opener=_Boom)
    got = []
    w.disconnected.connect(got.append)
    w.start()
    assert w.wait(2000)
    qapp.processEvents()
    assert got and "readiness" in got[0]


def test_worker_emits_disconnected_when_open_fails(qapp):
    def opener(*a, **k):
        raise serial.SerialException("could not open port")
    w = SerialWorker("X", opener=opener)
    got = []
    w.disconnected.connect(got.append)
    w.start()
    assert w.wait(2000)
    qapp.processEvents()
    assert got


def test_worker_emits_lines(qapp):
    class Lines:
        def __init__(self, *a, **k):
            self.lines = [b'{"evt":"hello"}\n', b"", b"x\n"]

        def readline(self):
            if self.lines:
                return self.lines.pop(0)
            raise serial.SerialException("done")

        def write(self, data):
            pass

        def close(self):
            pass

    w = SerialWorker("X", opener=Lines)
    got = []
    w.line_received.connect(got.append)
    w.start()
    assert w.wait(2000)
    qapp.processEvents()
    assert got == [b'{"evt":"hello"}\n', b"x\n"]
