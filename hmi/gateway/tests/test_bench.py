"""Banco de motores: lógica del ensayo (simulador) y flujo completo con el gateway."""
import random
import time

import pytest
from fastapi.testclient import TestClient

from app.main import create_app
from app.transports.demo_bench import CPW, SimBench


def run_capture(bench: SimBench, seq, period=2, motor="LR"):
    assert bench.run(motor, seq, period, estop=False) is None
    for _ in range(sum(s[1] for s in seq) + 50):
        bench.tick(estop=False)
    return bench.cap_rows


def test_capture_sampling():
    b = SimBench()
    b.enter()
    rows = run_capture(b, [[0, 100], [120, 400], [0, 100]], period=2)
    assert b.cap_state == 2 and len(rows) == 600 // 2 + 1
    ts = [r[0] for r in rows]
    assert ts[0] == 0 and all(t2 - t1 == 2000 for t1, t2 in zip(ts, ts[1:]))
    assert rows[0][1:3] == [0, 0]
    assert rows[50][1:3] == [120, 120]  # t = 100 ms: empieza el escalón
    assert rows[-1][1:3] == [0, 0]  # la última muestra deja el motor en 0
    # en régimen: K·(|u| − zona muerta) RPM
    k1, k2 = 200, 249
    rpm_r = (rows[k2][4] - rows[k1][4]) / CPW / ((k2 - k1) * 0.002) * 60
    assert 25 < rpm_r < 35, rpm_r


def test_capture_single_motor_and_estop():
    b = SimBench()
    b.enter()
    rows = run_capture(b, [[-80, 200]], period=5, motor="L")
    assert all(r[2] == 0 for r in rows) and rows[5][1] == -80
    assert rows[-1][3] < 0 and abs(rows[-1][4]) < 5  # sólo gira la izquierda, hacia atrás

    assert b.run("R", [[100, 1000]], 2, estop=False) is None
    for _ in range(100):
        b.tick(estop=False)
    b.tick(estop=True)
    assert b.cap_state == 3 and 0 < b.readable() < 501


def test_validation():
    b = SimBench()
    assert b.run("L", [[10, 10]], 2, False) == "el banco no está activo"
    b.enter()
    assert "motor" in b.run("X", [[10, 10]], 2, False)
    assert "muestras" in b.run("L", [[10, 60000]], 1, False)
    assert "period_ms" in b.run("L", [[10, 10]], 0, False)
    assert "Armar" in b.set_pwm(10, 10, estop=True)


@pytest.fixture
def client(tmp_path):
    app = create_app(db_path=tmp_path / "test.db", web_dist=tmp_path / "no-dist")
    with TestClient(app) as c:
        yield c


def recv_until(ws, pred, timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        msg = ws.receive_json()
        if pred(msg):
            return msg
    raise AssertionError("no llegó el mensaje esperado")


def test_bench_end_to_end(client):
    with client.websocket_connect("/ws") as ws:
        ws.receive_json()
        assert client.post("/api/connect", json={"transport": "demo"}).status_code == 200

        ws.send_json({"type": "cmd", "id": 1, "cmd": "bench", "op": "on"})
        ack = recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 1)
        assert ack["ok"] and ack["bench"]["on"]

        mot = recv_until(ws, lambda m: m.get("type") == "mot")
        assert {"pwmL", "pwmR", "cL", "cR", "rpmL", "rpmR", "eLA", "eLB", "eRA", "eRB", "lv", "cs"} <= set(mot["frames"][0])

        ws.send_json({"type": "cmd", "id": 2, "cmd": "deadband"})
        ack = recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 2)
        assert not ack["ok"] and "banco" in ack["err"]

        ws.send_json({"type": "cmd", "id": 3, "cmd": "bench", "op": "run", "motor": "R",
                      "seq": [[0, 100], [150, 300], [0, 100]], "period_ms": 2})
        ack = recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 3)
        assert ack["ok"] and ack["bench"]["cap"]["st"] == "running"

        ready = recv_until(ws, lambda m: m.get("type") == "captures_changed", timeout=6)
        cap = client.get(f"/api/captures/{ready['id']}").json()
        assert cap["state"] == "done" and cap["motor"] == "R" and cap["n"] == 251 and len(cap["rows"]) == 251
        assert cap["enc"]["cpw"] == CPW and cap["seq"] == [[0, 100], [150, 300], [0, 100]]

        csv = client.get(f"/api/captures/{ready['id']}.csv")
        assert csv.status_code == 200
        lines = csv.text.splitlines()
        assert lines[1] == "t_s,pwm_l,pwm_r,c_l,c_r,rpm_l,rpm_r" and len(lines) == 2 + 251

        ws.send_json({"type": "cmd", "id": 4, "cmd": "bench", "op": "off"})
        ack = recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 4)
        assert ack["ok"] and not ack["bench"]["on"] and ack["bench"]["estop"]

    assert len(client.get("/api/captures").json()) == 1
    assert client.delete(f"/api/captures/{cap['id']}").json()["ok"]
    assert client.get("/api/captures").json() == []


def test_rpm_loop_tracks_reference():
    b = SimBench()
    b.enter()
    assert b.set_rpm(25, estop=False) is None
    for _ in range(800):
        f = b.tick(estop=False)
    assert f["mode"] == "rpm" and abs(f["y"] - 25) < 3, f
    assert f["pwmL"] == f["pwmR"]  # izquierdo seguidor
    assert b.set_rpm(99, estop=False) is not None
    assert "kp" in b.set_cfg({"kp": 50})


def test_rpm_capture_end_to_end(client):
    with client.websocket_connect("/ws") as ws:
        ws.receive_json()
        assert client.post("/api/connect", json={"transport": "demo"}).status_code == 200
        ws.send_json({"type": "cmd", "id": 1, "cmd": "bench", "op": "on"})
        recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 1)
        ws.send_json({"type": "cmd", "id": 2, "cmd": "bench", "op": "rpmcfg", "kp": 2.0})
        ack = recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 2)
        assert ack["ok"] and ack["bench"]["rpmcfg"]["kp"] == 2.0
        ws.send_json({"type": "cmd", "id": 3, "cmd": "bench", "op": "run", "motor": "R", "kind": "rpm",
                      "seq": [[0, 100], [20.5, 600], [0, 100]], "period_ms": 2})
        ack = recv_until(ws, lambda m: m.get("type") == "ack" and m.get("id") == 3)
        assert ack["ok"], ack
        ready = recv_until(ws, lambda m: m.get("type") == "captures_changed", timeout=6)
        cap = client.get(f"/api/captures/{ready['id']}").json()
        assert cap["kind"] == "rpm" and cap["cfg"]["kp"] == 2.0 and len(cap["rows"][0]) == 6
        assert cap["rows"][200][5] == 205  # referencia · 10
        csv = client.get(f"/api/captures/{ready['id']}.csv").text.splitlines()
        assert csv[1].endswith(",ref_rpm") and csv[2 + 200].endswith(",20.5")


def _overshoot(ref_tau):
    random.seed(3)
    b = SimBench()
    b.enter()
    assert b.set_cfg({"ref_tau_ms": ref_tau}) is None
    b.set_rpm(10, estop=False)
    for _ in range(400):
        b.tick(estop=False)
    b.set_rpm(30, estop=False)
    peak = 0.0
    for _ in range(400):
        b.tick(estop=False)
        peak = max(peak, b.motors[1].rpm)
    return (peak - 30) / 20


def test_reference_model_reduces_overshoot():
    assert _overshoot(20) < _overshoot(0) - 0.05
