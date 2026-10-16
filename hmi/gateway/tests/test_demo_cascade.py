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
