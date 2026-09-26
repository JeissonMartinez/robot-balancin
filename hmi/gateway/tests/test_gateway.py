"""Pruebas de extremo a extremo con el robot simulado (transporte demo)."""
import time

import pytest
from fastapi.testclient import TestClient

from app.main import create_app


@pytest.fixture
def client(tmp_path):
    app = create_app(db_path=tmp_path / "test.db", web_dist=tmp_path / "no-dist")
    with TestClient(app) as c:
        yield c


def recv_until(ws, kind, timeout=3.0):
    end = time.time() + timeout
    while time.time() < end:
        msg = ws.receive_json()
        if msg.get("type") == kind:
            return msg
    raise AssertionError(f"no llegó {kind}")


def test_connect_stream_and_store(client):
    with client.websocket_connect("/ws") as ws:
        snap = ws.receive_json()
        assert snap["type"] == "snapshot" and snap["status"]["state"] == "disconnected"

        r = client.post("/api/connect", json={"transport": "demo"})
        assert r.status_code == 200, r.text
        st = r.json()
        assert st["state"] == "connected" and st["fw"] == "demo" and st["session_id"]

        tel = recv_until(ws, "tel")
        f = tel["frames"][0]
        assert {"seq", "t", "ang", "ref", "w", "pwm", "pwmM", "uP", "uI", "uD", "st"} <= set(f)

        ws.send_json({"type": "cmd", "id": 7, "cmd": "set", "params": {"kd_angle": 2.0}})
        ack = recv_until(ws, "ack")
        assert ack["id"] == 7 and ack["ok"] and ack["params"]["kd_angle"] == 2.0

        ws.send_json({"type": "cmd", "id": 8, "cmd": "set", "params": {"kd_angle": 99}})
        ack = recv_until(ws, "ack")
        assert ack["id"] == 8 and not ack["ok"] and "rango" in ack["err"]

        ws.send_json({"type": "cmd", "id": 9, "cmd": "tel", "on": False})
        ack = recv_until(ws, "ack")
        assert not ack["ok"] and "no permitido" in ack["err"]

        time.sleep(1.2)
        sid = st["session_id"]
        client.post("/api/disconnect")

    sessions = client.get("/api/sessions").json()
    assert sessions[0]["id"] == sid and sessions[0]["ended_at"] and sessions[0]["frames"] > 40
    rows = client.get(f"/api/sessions/{sid}/telemetry").json()
    seqs = [r["seq"] for r in rows]
    assert seqs == sorted(seqs) and len(set(seqs)) == len(seqs)
    kinds = [e["kind"] for e in client.get(f"/api/sessions/{sid}/events").json()]
    assert "connect" in kinds and kinds.count("cmd") == 2 and "disconnect" in kinds


def test_reboot_opens_new_session(client):
    r = client.post("/api/connect", json={"transport": "demo"})
    sid = r.json()["session_id"]
    link = client.app.state.link
    # Un hello que nadie pidió = el robot se reinició
    client.portal.call(lambda: _emit_hello(link))
    end = time.time() + 3
    while time.time() < end and link.session_id == sid:
        time.sleep(0.05)
    assert link.session_id != sid and link.state == "connected"
    client.post("/api/disconnect")


async def _emit_hello(link):
    link.transport._hello()


def test_connect_serial_requires_port(client):
    assert client.post("/api/connect", json={"transport": "serial"}).status_code == 400
