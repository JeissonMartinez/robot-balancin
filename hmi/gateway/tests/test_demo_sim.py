"""El robot simulado debe sostenerse con los parámetros de fábrica y caerse con ganancias absurdas."""
import json
import random
from pathlib import Path

from app.transports.demo import SimRobot

SCHEMA = json.loads((Path(__file__).parent.parent / "app/transports/demo_schema.json").read_text(encoding="utf-8"))
DEFAULTS = {d["key"]: d["def"] for d in SCHEMA}


def run(params, seconds=60):
    random.seed(1)
    sim = SimRobot(params)
    falls, max_ang = 0, 0.0
    for _ in range(int(seconds / 0.02)):
        f, fell = sim.step(params, estop=False)
        falls += fell
        if f["st"] == "ACTIVE":
            max_ang = max(max_ang, abs(f["ang"]))
    return falls, max_ang


def test_holds_with_factory_params():
    falls, max_ang = run(DEFAULTS)
    assert falls == 0 and max_ang < 5, (falls, max_ang)


def test_falls_without_control():
    falls, _ = run({**DEFAULTS, "kp_min": 0, "kp_max": 0, "kd_angle": 0})
    assert falls > 0
