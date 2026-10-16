"""Banco de motores del robot simulado: misma lógica que src/bench.cpp, con dos motores de
primer orden con zona muerta, para probar la pestaña Motores sin hardware.

Cada motor: RPM de rueda → objetivo K·(|u| − zona muerta) con constante de tiempo τ; con
PWM 0 frena más rápido (freno en corto del TB6612FNG). Las cuentas se integran con las
cuentas por vuelta de rueda del firmware y los flancos por canal son la mitad de las
cuentas (cuadratura x4: cada canal aporta la mitad de los flancos).
"""
from __future__ import annotations

import random

PPR_MOTOR = 11
DECODE = 4
GEAR = 119.0
CPW = PPR_MOTOR * DECODE * GEAR  # cuentas por vuelta de rueda (5236)
LIVE_MS = 20
MAX_SAMPLES = 5000
MAX_SEGMENTS = 16
MAX_PERIOD_MS = 20
MANUAL_TIMEOUT_MS = 30_000
GET_MAX_ROWS = 200

ENC = {"ppr": PPR_MOTOR, "decode": DECODE, "gear": GEAR, "cpw": CPW}
CAP_STATES = ("none", "running", "done", "aborted")


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


class SimBench:
    """Paso de 1 ms, igual que TaskBanco. Sin tiempo real (lo maneja DemoTransport)."""

    def __init__(self):
        # Izquierdo algo más lento y con más zona muerta, para que se distingan en las trazas
        self.motors = [SimMotor(0.30, 24, 0.075, 0.03, 8.0), SimMotor(0.31, 21, 0.065, 0.03, 8.0)]
        self.on = False
        self.manual = [0, 0]
        self.manual_ms = 0
        self.manual_expired = False
        self.ms = 0
        self.seq = 0
        self.cap_id = 0
        self.cap_state = 0
        self.cap_motor = 3
        self.cap_period = 2
        self.cap_seq: list[list[int]] = []
        self.cap_total = 0
        self.cap_rows: list[list[int]] = []
        self.cap_start: int | None = None
        self.cap_u = [0, 0]
        self.cap0 = (0, 0, 0)
        self._live = (0.0, 0.0)
        self._edges0 = [0.0, 0.0, 0.0, 0.0]

    # ------------------------------------------------------------ comandos
    def enter(self):
        if self.on:
            return
        self.on = True
        self.manual = [0, 0]
        self.cap_state, self.cap_total, self.cap_rows = 0, 0, []
        self._edges0 = [*self.motors[0].edges, *self.motors[1].edges]
        self._live = (self.motors[0].count, self.motors[1].count)

    def exit(self):
        self.on = False
        self.manual = [0, 0]
        self.cap_state, self.cap_total, self.cap_rows = 0, 0, []

    def set_pwm(self, l: int, r: int, estop: bool) -> str | None:
        if not self.on:
            return "el banco no está activo"
        if estop:
            return "parada de emergencia activa: Armar primero"
        if not all(isinstance(v, int) and -255 <= v <= 255 for v in (l, r)):
            return "PWM fuera de [-255, 255]"
        if self.cap_state == 1:
            return "hay un ensayo en curso"
        self.manual, self.manual_ms = [l, r], self.ms
        return None

    def run(self, motor: str, seq, period_ms: int, estop: bool) -> str | None:
        if not self.on:
            return "el banco no está activo"
        if estop:
            return "parada de emergencia activa: Armar primero"
        mask = {"L": 1, "R": 2, "LR": 3}.get(motor)
        if not mask:
            return 'motor debe ser "L", "R" o "LR"'
        if not isinstance(seq, list) or not 1 <= len(seq) <= MAX_SEGMENTS:
            return f"seq debe tener entre 1 y {MAX_SEGMENTS} tramos"
        for s in seq:
            if not (isinstance(s, list) and len(s) == 2 and all(isinstance(v, int) for v in s)):
                return "cada tramo debe ser [pwm, ms] con enteros"
            if not -255 <= s[0] <= 255 or not 1 <= s[1] <= 60000:
                return "tramo fuera de rango: pwm en [-255, 255], ms ≥ 1"
        if not isinstance(period_ms, int) or not 1 <= period_ms <= MAX_PERIOD_MS:
            return f"period_ms debe estar en [1, {MAX_PERIOD_MS}]"
        total = sum(s[1] for s in seq) // period_ms + 1
        if total > MAX_SAMPLES:
            return f"el ensayo pide {total} muestras; máximo {MAX_SAMPLES} (acortar o subir period_ms)"
        if self.cap_state == 1:
            return "ya hay un ensayo en curso"
        self.cap_id += 1
        self.cap_state, self.cap_motor, self.cap_period = 1, mask, period_ms
        self.cap_seq, self.cap_total, self.cap_rows = [list(s) for s in seq], total, []
        self.cap_start = None
        self.manual = [0, 0]
        return None

    def stop(self):
        self.manual = [0, 0]
        if self.cap_state == 1:
            self.cap_state = 3

    def status(self, estop: bool) -> dict:
        cap = {"id": self.cap_id, "st": CAP_STATES[self.cap_state], "n": len(self.cap_rows), "total": self.cap_total}
        if self.cap_state:
            cap.update(period_ms=self.cap_period, motor={1: "L", 2: "R", 3: "LR"}[self.cap_motor],
                       seq=self.cap_seq)
        return {"on": self.on, "estop": estop, "pwm": list(self.manual), "max_samples": MAX_SAMPLES, "cap": cap}

    def readable(self) -> int:
        return len(self.cap_rows) if self.cap_state in (2, 3) else 0

    # ------------------------------------------------------------ simulación
    def _segment_at(self, t_ms: int) -> int:
        end = 0
        for pwm, ms in self.cap_seq:
            end += ms
            if t_ms < end:
                return pwm
        return 0

    def tick(self, estop: bool) -> dict | None:
        """Avanza 1 ms. Devuelve la trama "mot" cada LIVE_ms."""
        self.ms += 1
        cl, cr = (round(m.count) for m in self.motors)
        if estop:
            self.manual = [0, 0]
            if self.cap_state == 1:
                self.cap_state = 3
        if self.cap_state == 1:
            if self.cap_start is None:
                self.cap_start, self.cap0 = self.ms, (self.ms, cl, cr)
            el = self.ms - self.cap_start
            if el % self.cap_period == 0:
                k = el // self.cap_period
                u = 0 if k + 1 >= self.cap_total else self._segment_at(k * self.cap_period)
                self.cap_u = [u if self.cap_motor & 1 else 0, u if self.cap_motor & 2 else 0]
                self.cap_rows.append([(self.ms - self.cap0[0]) * 1000, *self.cap_u, cl - self.cap0[1],
                                      cr - self.cap0[2]])
                if len(self.cap_rows) >= self.cap_total:
                    self.cap_state = 2
            u = list(self.cap_u)
        else:
            if any(self.manual) and self.ms - self.manual_ms > MANUAL_TIMEOUT_MS:
                self.manual, self.manual_expired = [0, 0], True
            u = list(self.manual)
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
