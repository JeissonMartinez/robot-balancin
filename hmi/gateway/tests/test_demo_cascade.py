"""RpmCascade (M3) en el robot simulado: con los valores de fábrica se sostiene; con el signo
del lazo de ángulo invertido, se cae."""
import json
import random
from pathlib import Path

from app.transports.demo_cascade import SimCascade

SCHEMA = json.loads((Path(__file__).parent.parent / "app/transports/demo_schema.json").read_text(encoding="utf-8"))
DEFAULTS = {d["key"]: d["def"] for d in SCHEMA}


def run(params, seconds=30, seed=1):
    random.seed(seed)
    sim = SimCascade()
    falls, max_ang, max_rr = 0, 0.0, 0.0
    for _ in range(int(seconds / 0.02)):
        f, fell = sim.step(params, estop=False)
        falls += fell
        if f["st"] == "ACTIVE":
            max_ang = max(max_ang, abs(f["ang"]))
            max_rr = max(max_rr, abs(f["rr"]))
    return falls, max_ang, max_rr


def test_holds_with_factory_params():
    falls, max_ang, max_rr = run(DEFAULTS)
    assert falls == 0 and max_ang < 5 and max_rr < 55, (falls, max_ang, max_rr)


def test_falls_with_wrong_sign():
    falls, _, _ = run({**DEFAULTS, "cascade_sign": 1})
    assert falls > 0


def test_pd_on_velocity_cannot_balance():
    """Sin término de ángulo proporcional suficiente (Kθ·r < g) el péndulo no se sostiene."""
    falls, _, _ = run({**DEFAULTS, "k_theta": 20})
    assert falls > 0


def _settle(sim, p, seconds):
    f = None
    for _ in range(int(seconds / 0.02)):
        f, fell = sim.step(p, estop=False)
        assert not fell
    return f


def test_returns_to_place_after_push():
    import math
    random.seed(4)
    p = {**DEFAULTS}
    sim = SimCascade()
    sim.th = 0.0
    _settle(sim, p, 2)
    sim.om += math.radians(40)  # empujón
    f = _settle(sim, p, 1)
    assert abs(f["x"]) > 2
    f = _settle(sim, p, 8)
    assert abs(f["x"]) < 1, f["x"]


def test_drive_moves_and_stops():
    random.seed(5)
    p = {**DEFAULTS}
    sim = SimCascade()
    _settle(sim, p, 2)
    x0 = sim.step(p, False)[0]["x"]
    for _ in range(150):  # 3 s pidiendo 10 RPM, renovando el mando como la HMI
        sim.drive(10, 0)
        sim.step(p, False)
    f = _settle(sim, p, 3)  # se suelta: se vence y se detiene
    assert f["x"] - x0 > 8 and abs(f["vr"]) < 0.5, f


def test_heading_correction():
    def drift(k_psi, k_r):
        random.seed(6)
        p = {**DEFAULTS, "k_psi": k_psi, "k_r": k_r}
        sim = SimCascade()
        _settle(sim, p, 2)
        h0 = sim.heading
        for _ in range(150):
            sim.drive(15, 0)
            sim.step(p, False)
        return abs(sim.heading - h0)
    assert drift(5, 2) < 3 < drift(0, 0)
