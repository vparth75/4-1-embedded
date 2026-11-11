from laserchat.model import ChatModel
from laserchat.protocol import DeviceError, Hello, Recv, Sent


def test_model_pending_then_sent():
    m = ChatModel()
    pending = m.add_pending("hi")
    assert pending.status == "pending" and pending.direction == "out"
    msg = m.apply(Sent("hi", "01101000 01101001"))
    assert msg is pending
    assert msg.status == "sent" and msg.bits.startswith("0110")


def test_model_sent_matches_oldest_with_same_text():
    m = ChatModel()
    a = m.add_pending("x")
    b = m.add_pending("x")
    assert m.apply(Sent("x", "")) is a
    assert b.status == "pending"


def test_model_recv_ok_and_corrupted():
    m = ChatModel()
    ok = m.apply(Recv(True, "yo", "b", [1, 2]))
    assert ok.direction == "in" and ok.status == "received" and ok.adc == [1, 2]
    assert m.apply(Recv(False, "h?", "", [1])).status == "corrupted"
    assert len(m.messages) == 2


def test_model_error_fails_pending():
    m = ChatModel()
    m.add_pending("x")
    assert m.apply(DeviceError("busy")).status == "failed"


def test_model_error_without_pending_returns_none():
    assert ChatModel().apply(DeviceError("bad command")) is None


def test_model_hello_returns_none():
    assert ChatModel().apply(Hello("fw", 1000, 2048)) is None


def test_model_fail_all_pending():
    m = ChatModel()
    m.add_pending("a")
    m.add_pending("b")
    m.apply(Recv(True, "in", "", []))
    m.fail_all_pending()
    assert [x.status for x in m.messages] == ["failed", "failed", "received"]
