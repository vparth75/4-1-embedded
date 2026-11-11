import pytest

from laserchat.protocol import (
    DeviceError, Hello, Recv, Sent, TextTooLong,
    cmd_hello, cmd_send, cmd_set_threshold, parse_line, utf8_len,
)


def test_parse_hello():
    line = '{"evt":"hello","fw":"laser-chat 1.0","bit_us":1000,"threshold":2048}'
    assert parse_line(line) == Hello("laser-chat 1.0", 1000, 2048)


def test_parse_sent_and_error():
    assert parse_line('{"evt":"sent","text":"hi","bits":"0110"}') == Sent("hi", "0110")
    assert parse_line('{"evt":"error","msg":"busy"}') == DeviceError("busy")


def test_parse_recv_bytes_with_bad_utf8():
    e = parse_line(b'{"evt":"recv","ok":false,"text":"h\xffi","bits":"","adc":[1,2]}\r\n')
    assert isinstance(e, Recv)
    assert e.ok is False
    assert e.adc == [1, 2]
    assert e.text.startswith("h") and e.text.endswith("i")


@pytest.mark.parametrize("line", [
    "", "garbage", "ets Jun  8 2016 rst:0x1", '{"evt":"recv"', "[1,2]",
    '{"evt":"nope"}', '{"evt":"sent"}', '{"evt":"recv","ok":true,"text":"x","bits":"","adc":"no"}',
    '{"evt":"hello","fw":"x","bit_us":"1000","threshold":2048}', b"\xff\xfe",
])
def test_parse_line_ignores_garbage(line):
    assert parse_line(line) is None


def test_cmd_send_bytes():
    assert cmd_send("hi") == b'{"cmd":"send","text":"hi"}\n'


def test_cmd_send_keeps_unicode_and_newline_escaped():
    out = cmd_send("é\n")
    assert out.endswith(b"\n") and out.count(b"\n") == 1
    assert "é".encode() in out


def test_cmd_send_rejects_over_64_utf8_bytes():
    cmd_send("a" * 64)
    assert utf8_len("😀" * 17) == 68
    with pytest.raises(TextTooLong):
        cmd_send("😀" * 17)


def test_cmd_send_rejects_empty():
    with pytest.raises(ValueError):
        cmd_send("")


def test_cmd_hello():
    assert cmd_hello() == b'{"cmd":"hello"}\n'


def test_cmd_set_threshold_range():
    assert b'"value":4095' in cmd_set_threshold(4095)
    assert b'"value":0' in cmd_set_threshold(0)
    with pytest.raises(ValueError):
        cmd_set_threshold(4096)
    with pytest.raises(ValueError):
        cmd_set_threshold(-1)
