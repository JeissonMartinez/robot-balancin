"""Robot simulado en la estructura RpmCascade (M3): la misma ley de control del firmware
(src/rpm_cascade.cpp) sobre un péndulo con la geometría medida y el motor de M1.

Modelo (el mismo del diseño, docs/control/M3_LAZO_ANGULO.md):
    (l + r) · θ'' = g · sen θ − r · φ''·cos θ      θ > 0: inclinado hacia adelante
    φ' (RPM de la rueda respecto del cuerpo): primer orden hacia K·(|u| − zona muerta), τ = 20 ms
El ángulo que «mide» el robot es −θ en grados (convenio del firmware: θ < 0 = adelante).
Sin tiempo real: lo maneja DemoTransport. Se prueba en tests/test_demo_cascade.py.
"""
from __future__ import annotations

import math
import random

from .demo_bench import CPW, RpmLoop

G = 9.81
L_COM = 0.045      # m, centro de masa sobre el eje (medido)
R_WHEEL = 0.0325   # m (rueda de 6.5 cm)
L_EFF = L_COM + R_WHEEL
MOTOR_K = 0.25     # RPM/PWM (M1)
MOTOR_DB = 7.5     # PWM: zona muerta ya girando (M0)
MOTOR_TAU = 0.020  # s (M1)
MOTOR_TAU_BRAKE = 0.012
CONTROL_MS = 5
PICKUP_S = 1.5
DIST_STD = 0.6     # rad/s², perturbación constante en cada período de 20 ms
RPM_FILTER_TAU = 0.0467


class SimCascade:
    def __init__(self):
        self.th = math.radians(3.0)  # rad, + adelante
        self.om = 0.0
        self.wheel = 0.0  # RPM relativa
        self.count = 0.0
        self.active = False
        self.down_time = PICKUP_S
        self.loop = RpmLoop()
        self.rpm_ref = 0.0
        self.accel = 0.0
        self.meas = 0.0
        self.u = 0
        self.ring: list[tuple[float, float]] = []
        self.t = 0.0
        self.rpm_f = 0.0
        self._last_count = 0.0
        self._ms = 0

    def _reset_control(self):
        self.loop.reset()
        self.rpm_ref = self.accel = self.meas = 0.0
        self.ring.clear()

    def _control(self, p: dict, angle: float, rate: float, dt: float) -> int:
        self.ring.append((self.t, self.count))
        back = max(1, (p["rpm_win"] + CONTROL_MS // 2) // CONTROL_MS)
        self.ring = self.ring[-16:]
        meas = 0.0
        if len(self.ring) > back:
            t0, c0 = self.ring[-1 - back]
            if self.t > t0:
                meas = (self.count - c0) / CPW / (self.t - t0) * 60
        e = angle - p["setpoint_angle"]
        self.accel = p["cascade_sign"] * (p["k_theta"] * e + p["k_omega"] * rate) + p["k_vel"] * self.rpm_ref
        self.rpm_ref = max(-p["rpm_ref_max"], min(p["rpm_ref_max"], self.rpm_ref + self.accel * dt))
        cfg = {"kp": p["rpm_kp"], "ki": p["rpm_ki"], "ff": True, "ff_k": p["rpm_ff_k"], "ff_b": p["rpm_ff_b"],
               "i_max": 255.0, "ref_tau_ms": p["rpm_ref_tau"], "ff_blend": 1.0, "reset_sign": False}
        return round(self.loop.step(cfg, self.rpm_ref, meas, dt))

    def step(self, p: dict, estop: bool, dt: float = 0.02) -> tuple[dict, bool]:
        """Avanza 20 ms (4 ciclos de control de 5 ms, física a 1 ms)."""
        fell = False
        dist = random.gauss(0, DIST_STD)
        for _ in range(int(dt * 1000)):
            self._ms += 1
            self.t += 0.001
            angle = -math.degrees(self.th) + random.gauss(0, 0.03)
            rate = -math.degrees(self.om) + random.gauss(0, 0.5)
            if estop and self.active:
                self.active = False
            if not self.active:
                self.down_time += 0.001
                if not estop and self.down_time >= PICKUP_S and abs(self.th) > math.radians(5):
                    self.th, self.om, self.wheel = math.radians(random.uniform(-3, 3)), 0.0, 0.0
                if not estop and abs(angle) < p["rearm_angle"]:
                    self.active = True
                    self._reset_control()
            if self._ms % CONTROL_MS == 0:
                self.u = self._control(p, angle, rate, CONTROL_MS / 1000) if self.active else 0
            u = self.u if self.active else 0
            # motor (rueda respecto del cuerpo)
            mag = max(0.0, abs(u) - MOTOR_DB) * MOTOR_K
            target = math.copysign(mag, u) if u else 0.0
            tau = MOTOR_TAU_BRAKE if u == 0 else MOTOR_TAU
            prev = self.wheel
            self.wheel += (target - self.wheel) * 0.001 / tau
            phi_dd = (self.wheel - prev) / 0.001 * 2 * math.pi / 60  # rad/s²
            self.count += self.wheel / 60 * CPW * 0.001
            # péndulo
            if self.active:
                th_dd = (G * math.sin(self.th) - R_WHEEL * phi_dd * math.cos(self.th)) / L_EFF + dist
                self.om += th_dd * 0.001
                self.th += self.om * 0.001
                if abs(math.degrees(self.th)) > p["max_angle"]:
                    self.active, fell, self.down_time = False, True, 0.0
                    self.th, self.om = math.copysign(math.radians(60), self.th), 0.0
            rpm_now = (self.count - self._last_count) / CPW / 0.001 * 60
            self._last_count = self.count
            self.rpm_f += (rpm_now - self.rpm_f) * 0.001 / (RPM_FILTER_TAU + 0.001)

        st = self.loop
        frame = {
            "ang": round(-math.degrees(self.th) + random.gauss(0, 0.05), 2), "ref": p["setpoint_angle"],
            "w": round(-math.degrees(self.om), 1), "pwm": float(self.u if self.active else 0),
            "pwmM": float(self.u if self.active else 0), "rpmL": round(self.rpm_f + random.gauss(0, 0.3), 1),
            "rpmR": round(self.rpm_f, 1), "kp": 0.0, "dt": CONTROL_MS / 1000,
            "uP": round(st.uP, 1), "uI": round(st.integ, 1), "uD": 0.0,
            "rr": round(self.rpm_ref, 2), "acc": round(self.accel, 1), "ym": round(st.meas, 2), "uF": round(st.uF, 1),
            "st": "ACTIVE" if self.active else ("ESTOP" if estop else "IDLE"),
        }
        return frame, fell
