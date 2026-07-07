from ai_chat_tui import protocol


def test_token():
    msg = protocol.parse("TOK|hello there")
    assert msg.type == "token"
    assert msg.payload == "hello there"


def test_done():
    msg = protocol.parse("DONE")
    assert msg.type == "done"


def test_error():
    msg = protocol.parse("ERR|wifi timeout")
    assert msg.type == "error"
    assert msg.payload == "wifi timeout"


def test_models():
    msg = protocol.parse("MODELS|llama-3,phi-3,qwen2")
    assert msg.type == "models"
    assert msg.payload.split(",") == ["llama-3", "phi-3", "qwen2"]


def test_unknown():
    msg = protocol.parse("garbage line")
    assert msg.type == "unknown"


def test_build_chat_roundtrip():
    line = protocol.build_chat("hi there | pipes are fine")
    assert line == "CHAT|hi there | pipes are fine"
    tag, rest = line.split("|", 1)
    assert tag == "CHAT"
    assert rest == "hi there | pipes are fine"


def test_build_select_model():
    assert protocol.build_select_model("llama-3") == "SELMODEL|llama-3"


def test_build_set_server():
    assert protocol.build_set_server("http://100.101.102.103:1234") == (
        "SETSERVER|http://100.101.102.103:1234"
    )
