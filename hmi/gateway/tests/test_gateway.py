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


def test_param_sets(client):
    # Sin robot conectado hay que enviar los parámetros
    assert client.post("/api/param-sets", json={"name": "vacío"}).status_code == 400
    r = client.post("/api/param-sets", json={"name": "Kd alto", "params": {"kd_angle": 2.5, "vieja": 1}})
    assert r.status_code == 200, r.text
    sid = r.json()["id"]
    assert client.post("/api/param-sets", json={"name": "Kd alto", "params": {"kd_angle": 1}}).status_code == 409
    r = client.post("/api/param-sets", json={"name": "Kd alto", "params": {"kd_angle": 2.5, "vieja": 1},
                                             "overwrite": True})
    assert r.json()["id"] == sid

    # Aplicar: la clave desconocida se omite y se informa
    client.post("/api/connect", json={"transport": "demo"})
    r = client.post(f"/api/param-sets/{sid}/apply").json()
    assert r["ok"] and r["params"]["kd_angle"] == 2.5 and r["ignored"] == ["vieja"]

    # Guardar los activos del robot
    r = client.post("/api/param-sets", json={"name": "actual", "notes": "n"}).json()
    assert r["params"]["kd_angle"] == 2.5 and r["fw"] == "demo"
    names = [s["name"] for s in client.get("/api/param-sets").json()]
    assert names == ["actual", "Kd alto"]

    assert client.delete(f"/api/param-sets/{sid}").status_code == 200
    assert client.delete(f"/api/param-sets/{sid}").status_code == 404
    session = client.get("/api/status").json()["session_id"]
    client.post("/api/disconnect")
    ev = client.get(f"/api/sessions/{session}/events").json()
    cmd = [e for e in ev if e["kind"] == "cmd"][0]
    assert cmd["payload"]["source"] == "juego «Kd alto»"
    assert any(e["kind"] == "param_set_saved" for e in ev)


def test_disconnect_when_idle_records_nothing(client):
    client.post("/api/disconnect")
    client.post("/api/connect", json={"transport": "demo"})
    session = client.get("/api/status").json()["session_id"]
    client.post("/api/disconnect")
    client.post("/api/disconnect")
    kinds = [e["kind"] for e in client.get(f"/api/sessions/{session}/events").json()]
    assert kinds.count("disconnect") == 1


def test_history(client):
    client.post("/api/connect", json={"transport": "demo"})
    sid = client.get("/api/status").json()["session_id"]
    time.sleep(1.2)
    assert client.delete(f"/api/sessions/{sid}").status_code == 409  # en curso
    client.post("/api/disconnect")

    s = client.get(f"/api/sessions/{sid}").json()
    assert s["frames"] > 40 and s["t_last"] > s["t_first"] and s["n_events"] >= 2

    r = client.patch(f"/api/sessions/{sid}", json={"notes": "  Kd alto  "}).json()
    assert r["notes"] == "Kd alto"
    assert [x["id"] for x in client.get("/api/sessions?q=kd").json()] == [sid]
    assert client.get("/api/sessions?transport=serial").json() == []

    cols = client.get(f"/api/sessions/{sid}/columns?max_points=10").json()
    assert cols["total"] == s["frames"] and len(cols["columns"]["ang"]) <= 10 and cols["step"] > 1

    page = client.get(f"/api/sessions/{sid}/telemetry?limit=5&offset=5").json()
    assert len(page) == 5

    csv = client.get(f"/api/sessions/{sid}/telemetry.csv")
    lines = csv.text.strip().split("\n")
    assert lines[0].startswith("t_s,host_ts,seq,t_ms,ang") and len(lines) == s["frames"] + 1
    assert lines[1].startswith("0.000,")

    assert client.delete(f"/api/sessions/{sid}").status_code == 200
    assert client.get(f"/api/sessions/{sid}").status_code == 404
    assert client.get(f"/api/sessions/{sid}/telemetry").json() == []


def test_static_is_compressed(tmp_path):
    dist = tmp_path / "dist"
    dist.mkdir()
    (dist / "index.html").write_text("<html>" + "x" * 5000 + "</html>")
    with TestClient(create_app(db_path=tmp_path / "t.db", web_dist=dist)) as c:
        r = c.get("/", headers={"accept-encoding": "gzip"})
        assert r.status_code == 200 and r.headers.get("content-encoding") == "gzip"


def test_access_key(tmp_path):
    from starlette.websockets import WebSocketDisconnect as WSD

    with TestClient(create_app(db_path=tmp_path / "t.db", web_dist=tmp_path / "x", clave="secreta")) as c:
        assert c.get("/api/health").status_code == 200
        assert c.get("/api/auth").json() == {"required": True, "ok": False}
        assert c.get("/api/sessions").status_code == 401
        with pytest.raises(WSD):
            with c.websocket_connect("/ws") as ws:
                ws.receive_json()
        assert c.post("/api/auth", json={"clave": "otra"}).status_code == 401
        assert c.post("/api/auth", json={"clave": "secreta"}).status_code == 200
        # la cookie queda en el cliente
        assert c.get("/api/auth").json()["ok"] is True
        assert c.get("/api/sessions").status_code == 200
        with c.websocket_connect("/ws") as ws:
            assert ws.receive_json()["type"] == "snapshot"
        assert c.get("/api/sessions", headers={"authorization": "Bearer secreta"}, cookies={}).status_code == 200


def test_no_key_is_open(client):
    assert client.get("/api/auth").json() == {"required": False, "ok": True}
