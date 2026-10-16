"""Banco de motores del robot simulado: misma lógica que src/bench.cpp, con dos motores de
primer orden con zona muerta, para probar la pestaña Motores sin hardware.

Cada motor: RPM de rueda → objetivo K·(|u| − zona muerta) con constante de tiempo τ; con
PWM 0 frena más rápido (freno en corto del TB6612FNG). Las cuentas se integran con las
cuentas por vuelta de rueda del firmware y los flancos por canal son la mitad de las
cuentas (cuadratura x4: cada canal aporta la mitad de los flancos).

Modo RPM (M2, fw ≥ 0.7.0): el mismo lazo del firmware sobre la rueda derecha (prealimentación +
PI con integración condicional), con el izquierdo como seguidor.
"""
from __future__ import annotations

import math
import random

PPR_MOTOR = 11
DECODE = 4
CPW = 2618.0  # cuentas por vuelta de rueda, medidas en el robot (PPR_WHEEL en config.h)
GEAR = CPW / (PPR_MOTOR * DECODE)
LIVE_MS = 20
MAX_SAMPLES = 4000
MAX_SEGMENTS = 16
MAX_PERIOD_MS = 20
MANUAL_TIMEOUT_MS = 30_000
GET_MAX_ROWS = 200
MAX_RPM = 60.0
RING = 64

ENC = {"ppr": PPR_MOTOR, "decode": DECODE, "gear": GEAR, "cpw": CPW}
CAP_STATES = ("none", "running", "done", "aborted")
DEFAULT_CFG = {"kp": 1.33, "ki": 67.0, "ts_ms": 5, "win_ms": 10, "ff": True, "ff_k": 0.25, "ff_b": 1.8,
               "i_max": 255.0, "follow": True, "ref_tau_ms": 20}


class SimMotor:
    def __init__(self, gain: float, deadband: float, tau: float, tau_brake: float, noise: float):
        self.gain, self.deadband, self.tau, self.tau_brake, self.noise = gain, deadband, tau, tau_brake, noise
        self.rpm = 0.0
        self.count = 0.0
        self.edges = [0.0, 0.0]

    def step(self, u: int, dt: float):
        mag = max(0.0, abs(u) - self.deadband) * self.gain
        target = mag if u >= 0 else -mag
        tau = self.tau_brake if u == 0 else self.tau
        self.rpm += (target - self.rpm) * dt / tau
        self.rpm += random.gauss(0, self.noise) * dt
        dc = self.rpm / 60.0 * CPW * dt
        self.count += dc
        self.edges[0] += abs(dc) / 2
        self.edges[1] += abs(dc) / 2

    def levels(self) -> int:
        """Nivel de A (bit 0) y B (bit 1) según la fase de la cuadratura."""
        return (0b00, 0b01, 0b11, 0b10)[int(self.count) % 4]


def _sgn(v: float) -> float:
    return 1.0 if v > 0 else (-1.0 if v < 0 else 0.0)


class RpmLoop:
    """El lazo de RpmLoop::step en src/bench.cpp."""

    def __init__(self):
        self.reset()
        self.meas = 0.0

    def reset(self):
        self.integ = self.uF = self.uP = self.out = 0.0
        self.last_ref = self.ref_f = 0.0

    def step(self, c: dict, ref: float, y: float, dt: float) -> float:
        self.meas = y
        if ref == 0:
            self.reset()
            return 0.0
        if _sgn(ref) != _sgn(self.last_ref):
            self.integ = 0.0
        self.last_ref = ref
        if c["ref_tau_ms"] > 0:
            self.ref_f += (ref - self.ref_f) * (1 - math.exp(-dt / (c["ref_tau_ms"] / 1000)))
        else:
            self.ref_f = ref
        e = self.ref_f - y
        self.uF = _sgn(ref) * (abs(ref) + c["ff_b"]) / c["ff_k"] if c["ff"] and c["ff_k"] > 0 else 0.0
        self.uP = c["kp"] * e
        cand = max(-c["i_max"], min(c["i_max"], self.integ + c["ki"] * e * dt))
        raw = self.uF + self.uP + cand
        if not ((raw > 255 and e > 0) or (raw < -255 and e < 0)):
            self.integ = cand
        self.out = max(-255.0, min(255.0, self.uF + self.uP + self.integ))
        return self.out


class SimBench:
    """Paso de 1 ms, igual que TaskBanco. Sin tiempo real (lo maneja DemoTransport)."""

    def __init__(self):
        # Ganancia y τ de M1; el izquierdo con algo más de zona muerta para que se distingan
        self.motors = [SimMotor(0.25, 9, 0.022, 0.012, 8.0), SimMotor(0.25, 7.5, 0.020, 0.012, 8.0)]
        self.on = False
        self.mode_rpm = False
        self.manual = [0, 0]
        self.manual_ref = 0.0
        self.manual_ms = 0
        self.manual_expired = False
        self.cfg = dict(DEFAULT_CFG)
        self.loop = RpmLoop()
        self.rpm_u = 0
        self.was_rpm = False
        self.ring: list[tuple[int, float]] = [(0, 0.0)] * RING
        self.ms = 0
        self.seq = 0
        self.cap_id = 0
        self.cap_state = 0
        self.cap_rpm = False
        self.cap_motor = 3
        self.cap_period = 2
        self.cap_seq: list[list] = []
        self.cap_total = 0
        self.cap_rows: list[list] = []
        self.cap_start: int | None = None
        self.cap_u = [0, 0]
        self.cap_ref = 0.0
        self.cap0 = (0, 0, 0)
        self._live = (0.0, 0.0)
        self._edges0 = [0.0, 0.0, 0.0, 0.0]

    # ------------------------------------------------------------ comandos
    def enter(self):
        if self.on:
            return
        self.on = True
        self.mode_rpm = False
        self.manual, self.manual_ref = [0, 0], 0.0
        self.cap_state, self.cap_total, self.cap_rows = 0, 0, []
        self._edges0 = [*self.motors[0].edges, *self.motors[1].edges]
        self._live = (self.motors[0].count, self.motors[1].count)

    def exit(self):
        self.on = False
        self.manual, self.manual_ref = [0, 0], 0.0
        self.cap_state, self.cap_total, self.cap_rows = 0, 0, []

    def _can_drive(self, estop: bool) -> str | None:
        if not self.on:
            return "el banco no está activo"
        if estop:
            return "parada de emergencia activa: Armar primero"
        return None

    def set_pwm(self, l: int, r: int, estop: bool) -> str | None:
        if err := self._can_drive(estop):
            return err
        if not all(isinstance(v, int) and -255 <= v <= 255 for v in (l, r)):
            return "PWM fuera de [-255, 255]"
        if self.cap_state == 1:
            return "hay un ensayo en curso"
        self.mode_rpm, self.manual_ref = False, 0.0
        self.manual, self.manual_ms = [l, r], self.ms
        return None

    def set_rpm(self, ref: float, estop: bool) -> str | None:
        if err := self._can_drive(estop):
            return err
        if not isinstance(ref, (int, float)) or isinstance(ref, bool) or abs(ref) > MAX_RPM:
            return f"RPM fuera de [-{MAX_RPM:g}, {MAX_RPM:g}]"
        if self.cap_state == 1:
            return "hay un ensayo en curso"
        self.mode_rpm, self.manual = True, [0, 0]
        self.manual_ref, self.manual_ms = float(ref), self.ms
        return None

    def set_cfg(self, changes: dict) -> str | None:
        c = dict(self.cfg)
        for k, v in changes.items():
            if k not in c:
                continue
            if k in ("ff", "follow"):
                if not isinstance(v, bool):
                    return f"{k} debe ser true o false"
            elif isinstance(v, bool) or not isinstance(v, (int, float)):
                return f"{k} debe ser numérico"
            c[k] = v
        checks = [("kp", 0, 20), ("ki", 0, 2000), ("ts_ms", 1, 20), ("win_ms", 1, RING - 4), ("ff_k", 0.05, 1),
                  ("ff_b", 0, 20), ("i_max", 0, 255), ("ref_tau_ms", 0, 200)]
        for k, lo, hi in checks:
            if not lo <= c[k] <= hi:
                return f"{k} debe estar en [{lo:g}, {hi:g}]"
        c["ts_ms"], c["win_ms"], c["ref_tau_ms"] = int(c["ts_ms"]), int(c["win_ms"]), int(c["ref_tau_ms"])
        self.cfg = c
        return None

    def run(self, motor: str, seq, period_ms: int, estop: bool, kind: str = "pwm") -> str | None:
        if err := self._can_drive(estop):
            return err
        mask = {"L": 1, "R": 2, "LR": 3}.get(motor)
        if not mask:
            return 'motor debe ser "L", "R" o "LR"'
        if kind not in ("pwm", "rpm"):
            return 'kind debe ser "pwm" o "rpm"'
        rpm = kind == "rpm"
        if not isinstance(seq, list) or not 1 <= len(seq) <= MAX_SEGMENTS:
            return f"seq debe tener entre 1 y {MAX_SEGMENTS} tramos"
        limit = MAX_RPM if rpm else 255
        for s in seq:
            ok_val = isinstance(s, list) and len(s) == 2 and (
                isinstance(s[0], (int, float)) if rpm else isinstance(s[0], int)) and isinstance(s[1], int)
            if not ok_val:
                return "cada tramo debe ser [rpm, ms] (ms entero)" if rpm else "cada tramo debe ser [pwm, ms] con enteros"
            if abs(s[0]) > limit or not 1 <= s[1] <= 60000:
                return "tramo fuera de rango"
        if not isinstance(period_ms, int) or not 1 <= period_ms <= MAX_PERIOD_MS:
            return f"period_ms debe estar en [1, {MAX_PERIOD_MS}]"
        total = sum(s[1] for s in seq) // period_ms + 1
        if total > MAX_SAMPLES:
            return f"el ensayo pide {total} muestras; máximo {MAX_SAMPLES} (acortar o subir period_ms)"
        if self.cap_state == 1:
            return "ya hay un ensayo en curso"
        self.cap_id += 1
        self.cap_state, self.cap_rpm, self.cap_motor, self.cap_period = 1, rpm, mask, period_ms
        self.cap_seq, self.cap_total, self.cap_rows = [list(s) for s in seq], total, []
        self.cap_start, self.cap_ref = None, 0.0
        self.manual, self.manual_ref = [0, 0], 0.0
        return None

    def stop(self):
        self.manual, self.manual_ref = [0, 0], 0.0
        if self.cap_state == 1:
            self.cap_state = 3

    def status(self, estop: bool) -> dict:
        cap = {"id": self.cap_id, "st": CAP_STATES[self.cap_state], "n": len(self.cap_rows), "total": self.cap_total}
        if self.cap_state:
            cap.update(kind="rpm" if self.cap_rpm else "pwm", period_ms=self.cap_period,
                       motor={1: "L", 2: "R", 3: "LR"}[self.cap_motor], seq=self.cap_seq)
            if self.cap_rpm:
                cap["rpmcfg"] = {k: self.cfg[k] for k in ("kp", "ki", "ts_ms", "win_ms", "ff", "follow", "ref_tau_ms")}
        return {"on": self.on, "estop": estop, "mode": "rpm" if self.mode_rpm else "pwm", "pwm": list(self.manual),
                "ref": self.manual_ref, "max_samples": MAX_SAMPLES, "max_rpm": MAX_RPM, "rpmcfg": dict(self.cfg),
                "cap": cap}

    def readable(self) -> int:
        return len(self.cap_rows) if self.cap_state in (2, 3) else 0

    # ------------------------------------------------------------ simulación
    def _segment_at(self, t_ms: int) -> float:
        end = 0
        for val, ms in self.cap_seq:
            end += ms
            if t_ms < end:
                return val
        return 0

    def tick(self, estop: bool) -> dict | None:
        """Avanza 1 ms. Devuelve la trama "mot" cada LIVE_MS."""
        self.ms += 1
        cl, cr = (round(m.count) for m in self.motors)
        self.ring[self.ms % RING] = (self.ms, cr)
        if estop:
            self.manual, self.manual_ref = [0, 0], 0.0
            if self.cap_state == 1:
                self.cap_state = 3
        sample_k = None
        if self.cap_state == 1:
            if self.cap_start is None:
                self.cap_start, self.cap0 = self.ms, (self.ms, cl, cr)
            el = self.ms - self.cap_start
            if el % self.cap_period == 0:
                sample_k = el // self.cap_period
                v = 0 if sample_k + 1 >= self.cap_total else self._segment_at(sample_k * self.cap_period)
                if self.cap_rpm:
                    self.cap_ref = float(v)
                else:
                    self.cap_u = [v if self.cap_motor & 1 else 0, v if self.cap_motor & 2 else 0]
            rpm_active, ref, u = self.cap_rpm, self.cap_ref, list(self.cap_u)
        else:
            if (any(self.manual) or self.manual_ref) and self.ms - self.manual_ms > MANUAL_TIMEOUT_MS:
                self.manual, self.manual_ref, self.manual_expired = [0, 0], 0.0, True
            rpm_active, ref, u = self.mode_rpm, self.manual_ref, list(self.manual)

        c = self.cfg
        if rpm_active:
            if not self.was_rpm:
                self.loop.reset()
                self.rpm_u = 0
            ts = c["ts_ms"]
            if self.ms % ts == 0 and self.ms > c["win_ms"]:
                t0, c0 = self.ring[(self.ms - c["win_ms"]) % RING]
                dt = (self.ms - t0) / 1000
                y = (cr - c0) / CPW / dt * 60 if dt > 0 else 0.0
                self.rpm_u = round(self.loop.step(c, ref, y, ts / 1000))
            if ref == 0:
                self.rpm_u = 0
            u = [self.rpm_u if c["follow"] else 0, self.rpm_u]
        elif self.was_rpm:
            self.loop.reset()
        self.was_rpm = rpm_active

        if sample_k is not None and self.cap_state == 1:
            self.cap_rows.append([(self.ms - self.cap0[0]) * 1000, *u, cl - self.cap0[1], cr - self.cap0[2],
                                  round(ref * 10)])
            if len(self.cap_rows) >= self.cap_total:
                self.cap_state = 2
        if estop:
            u = [0, 0]
        for m, v in zip(self.motors, u):
            m.step(v, 0.001)

        if self.ms % LIVE_MS:
            return None
        dt = LIVE_MS / 1000
        rpm = [(cl - self._live[0]) / CPW / dt * 60, (cr - self._live[1]) / CPW / dt * 60]
        self._live = (cl, cr)
        e = [self.motors[0].edges[0], self.motors[0].edges[1], self.motors[1].edges[0], self.motors[1].edges[1]]
        lv = self.motors[0].levels() | (self.motors[1].levels() << 2)
        frame = {
            "type": "mot", "seq": self.seq, "t": self.ms, "pwmL": u[0], "pwmR": u[1], "cL": cl, "cR": cr,
            "rpmL": round(rpm[0], 2), "rpmR": round(rpm[1], 2),
            "eLA": int(e[0] - self._edges0[0]), "eLB": int(e[1] - self._edges0[1]),
            "eRA": int(e[2] - self._edges0[2]), "eRB": int(e[3] - self._edges0[3]), "lv": lv, "es": int(estop),
            "mode": "rpm" if rpm_active else "pwm", "ref": round(ref if rpm_active else 0.0, 2),
            "rf": round(self.loop.ref_f if rpm_active else 0.0, 2),
            "y": round(self.loop.meas, 2), "uF": round(self.loop.uF if rpm_active else 0.0, 1),
            "uP": round(self.loop.uP if rpm_active else 0.0, 1), "uI": round(self.loop.integ if rpm_active else 0.0, 1),
            "cs": CAP_STATES[self.cap_state], "ci": self.cap_id, "ck": len(self.cap_rows), "cn": self.cap_total,
        }
        self.seq += 1
        return frame

    def idle(self, ms: int):
        """Fuera del banco los motores también se frenan (el control simulado no los mueve)."""
        for _ in range(ms):
            for m in self.motors:
                m.step(0, 0.001)
        self.ms += ms
