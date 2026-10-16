"""Robot simulado en la estructura RpmCascade (M3 + M4): la misma ley de control del firmware
(src/rpm_cascade.cpp) sobre un péndulo con la geometría medida y los motores de M1.

Usa las ganancias del modelo (Kθ 90, Kx 40). El robot real equilibra con Kθ ≈ 42 y Kx ≈ 152
(sesión 55), que en este modelo se caen: el robot real necesita bastante menos Kθ que lo que
predice el modelo (docs/control/M4 §11).

Modelo (el mismo del diseño, docs/control/M3_LAZO_ANGULO.md y M4_POSICION_VELOCIDAD.md):
    (l + r) · θ'' = g · sen θ − r · φ''·cos θ      θ > 0: inclinado hacia adelante
    φ' (RPM media de las ruedas respecto del cuerpo); cada rueda: primer orden hacia
    K·(|u| − zona muerta), τ = 20 ms. El izquierdo un 8 % más débil, para que sin corrección de
    rumbo el robot gire solo (como el robot real con el izquierdo en lazo abierto).
    Giro: ω_z = (v_izq − v_der)/vía  (con este convenio, yaw_sign = +1 corrige).
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
TRACK = 0.116      # m, vía (de la escena de la HMI)
L_EFF = L_COM + R_WHEEL
MOTOR_K = (0.25 * 0.92, 0.25)  # RPM/PWM: izquierdo, derecho
MOTOR_DB = 7.5     # PWM: zona muerta ya girando (M0)
MOTOR_TAU = 0.020  # s (M1)
MOTOR_TAU_BRAKE = 0.012
CONTROL_MS = 5
PICKUP_S = 1.5
DIST_STD = 0.6     # rad/s², perturbación constante en cada período de 20 ms
RPM_FILTER_TAU = 0.0467
DRIVE_TIMEOUT_S = 1.0
HOME_GAIN = 0.5
YAW_DELTA_MAX = 80.0
CM_PER_TURN = 2 * math.pi * R_WHEEL * 100


def _approach(cur: float, target: float, step: float) -> float:
    return min(cur + step, target) if target > cur else max(cur - step, target)


class SimCascade:
    def __init__(self):
        self.th = math.radians(3.0)  # rad, + adelante
        self.om = 0.0
        self.wheels = [0.0, 0.0]  # RPM relativas izq, der
        self.counts = [0.0, 0.0]
        self.heading = 0.0  # ° (real)
        self.active = False
        self.down_time = PICKUP_S
        self.loop = RpmLoop()
        self.u = (0, 0)
        self.ring: list[tuple[float, float]] = []
        self.t = 0.0
        self.rpm_f = [0.0, 0.0]
        self._last = [0.0, 0.0]
        self._ms = 0
        # mando de conducción
        self.v_cmd = self.r_cmd = 0.0
        self.cmd_t = -10.0
        self.homing = False
        self._reset_control()

    # ------------------------------------------------------------ conducción
    def drive(self, v: float, r: float):
        self.v_cmd, self.r_cmd, self.homing, self.cmd_t = float(v), float(r), False, self.t

    def home(self):
        self.v_cmd = self.r_cmd = 0.0
        self.homing = True

    def _reset_control(self):
        self.loop.reset()
        self.rpm_ref = self.accel = 0.0
        self.ring = []
        self.c0 = self.counts[1]
        self.x = self.x_ref = self.v_ref = 0.0
        self.psi = self.psi_ref = self.r_ref = self.dl = 0.0
        self.v_cmd = self.r_cmd = 0.0
        self.homing = False

    def _control(self, p: dict, angle: float, rate: float, yaw: float, dt: float) -> tuple[int, int]:
        cr = self.counts[1]
        self.ring.append((self.t, cr))
        self.ring = self.ring[-16:]
        back = max(1, (p["rpm_win"] + CONTROL_MS // 2) // CONTROL_MS)
        meas = 0.0
        if len(self.ring) > back:
            t0, c0 = self.ring[-1 - back]
            if self.t > t0:
                meas = (cr - c0) / CPW / (self.t - t0) * 60
        self.x = (cr - self.c0) / CPW
        # conducción
        if not self.homing and (self.v_cmd or self.r_cmd) and self.t - self.cmd_t > DRIVE_TIMEOUT_S:
            self.v_cmd = self.r_cmd = 0.0
        if self.homing and abs(self.x_ref) < 0.005 and abs(self.v_ref) < 0.5:
            self.homing = False
        vc = max(-p["v_ref_max"] / 2, min(p["v_ref_max"] / 2, -HOME_GAIN * self.x_ref * 60)) if self.homing else self.v_cmd
        vc = max(-p["v_ref_max"], min(p["v_ref_max"], vc))
        rc = max(-p["r_ref_max"], min(p["r_ref_max"], self.r_cmd))
        self.v_ref = _approach(self.v_ref, vc, p["v_accel"] * dt)
        self.r_ref = _approach(self.r_ref, rc, 2 * p["r_ref_max"] * dt)
        self.x_ref += self.v_ref / 60 * dt
        # ángulo + velocidad + posición
        e = angle - p["setpoint_angle"]
        self.accel = (p["cascade_sign"] * (p["k_theta"] * e + p["k_omega"] * rate)
                      + p["k_vel"] * (self.rpm_ref - self.v_ref) + p["k_x"] * (self.x - self.x_ref))
        self.rpm_ref = max(-p["rpm_ref_max"], min(p["rpm_ref_max"], self.rpm_ref + self.accel * dt))
        cfg = {"kp": p["rpm_kp"], "ki": p["rpm_ki"], "ff": True, "ff_k": p["rpm_ff_k"], "ff_b": p["rpm_ff_b"],
               "i_max": 255.0, "ref_tau_ms": p["rpm_ref_tau"], "ff_blend": 1.0, "reset_sign": False}
        u = self.loop.step(cfg, self.rpm_ref, meas, dt)
        # rumbo
        self.psi += yaw * dt
        self.psi_ref += self.r_ref * dt
        self.dl = p["yaw_sign"] * (p["k_psi"] * (self.psi_ref - self.psi) + p["k_r"] * (self.r_ref - yaw))
        self.dl = max(-YAW_DELTA_MAX, min(YAW_DELTA_MAX, self.dl))
        return round(max(-255, min(255, u + self.dl))), round(u)

    def step(self, p: dict, estop: bool, dt: float = 0.02) -> tuple[dict, bool]:
        """Avanza 20 ms (4 ciclos de control de 5 ms, física a 1 ms)."""
        fell = False
        dist = random.gauss(0, DIST_STD)
        for _ in range(int(dt * 1000)):
            self._ms += 1
            self.t += 0.001
            angle = -math.degrees(self.th) + random.gauss(0, 0.03)
            rate = -math.degrees(self.om) + random.gauss(0, 0.5)
            v = [w / 60 * 2 * math.pi * R_WHEEL for w in self.wheels]
            yaw_true = math.degrees((v[0] - v[1]) / TRACK)
            yaw = yaw_true + random.gauss(0, 0.3)
            if estop and self.active:
                self.active = False
            if not self.active:
                self.down_time += 0.001
                if not estop and self.down_time >= PICKUP_S and abs(self.th) > math.radians(5):
                    self.th, self.om, self.wheels = math.radians(random.uniform(-3, 3)), 0.0, [0.0, 0.0]
                if not estop and abs(angle) < p["rearm_angle"]:
                    self.active = True
                    self._reset_control()
            if self._ms % CONTROL_MS == 0:
                self.u = self._control(p, angle, rate, yaw, CONTROL_MS / 1000) if self.active else (0, 0)
            us = self.u if self.active else (0, 0)
            prev = sum(self.wheels) / 2
            for i, u in enumerate(us):
                mag = max(0.0, abs(u) - MOTOR_DB) * MOTOR_K[i]
                target = math.copysign(mag, u) if u else 0.0
                tau = MOTOR_TAU_BRAKE if u == 0 else MOTOR_TAU
                self.wheels[i] += (target - self.wheels[i]) * 0.001 / tau
                self.counts[i] += self.wheels[i] / 60 * CPW * 0.001
            phi_dd = (sum(self.wheels) / 2 - prev) / 0.001 * 2 * math.pi / 60  # rad/s², rueda media
            self.heading += yaw_true * 0.001
            if self.active:
                th_dd = (G * math.sin(self.th) - R_WHEEL * phi_dd * math.cos(self.th)) / L_EFF + dist
                self.om += th_dd * 0.001
                self.th += self.om * 0.001
                if abs(math.degrees(self.th)) > p["max_angle"]:
                    self.active, fell, self.down_time = False, True, 0.0
                    self.th, self.om = math.copysign(math.radians(60), self.th), 0.0
            for i in range(2):
                rpm_now = (self.counts[i] - self._last[i]) / CPW / 0.001 * 60
                self._last[i] = self.counts[i]
                self.rpm_f[i] += (rpm_now - self.rpm_f[i]) * 0.001 / (RPM_FILTER_TAU + 0.001)

        st = self.loop
        act = self.active
        frame = {
            "ang": round(-math.degrees(self.th) + random.gauss(0, 0.05), 2), "ref": p["setpoint_angle"],
            "w": round(-math.degrees(self.om), 1), "pwm": float(self.u[1] if act else 0),
            "pwmM": float(self.u[1] if act else 0), "rpmL": round(self.rpm_f[0], 1),
            "rpmR": round(self.rpm_f[1], 1), "kp": 0.0, "dt": CONTROL_MS / 1000,
            "uP": round(st.uP, 1), "uI": round(st.integ, 1), "uD": 0.0,
            "rr": round(self.rpm_ref, 2), "acc": round(self.accel, 1), "ym": round(st.meas, 2), "uF": round(st.uF, 1),
            "x": round(self.x * CM_PER_TURN, 2), "xr": round(self.x_ref * CM_PER_TURN, 2), "vr": round(self.v_ref, 1),
            "psi": round(self.psi, 1), "psir": round(self.psi_ref, 1), "wz": round(yaw_true, 1), "dl": round(self.dl, 1),
            "st": "ACTIVE" if act else ("ESTOP" if estop else "IDLE"),
        }
        return frame, fell
