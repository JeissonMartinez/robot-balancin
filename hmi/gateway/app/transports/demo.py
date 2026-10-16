"""Robot simulado que habla el protocolo v1, para desarrollar y probar sin hardware.

No es un modelo del balancín (eso es el Simulador_Balancin): es un péndulo
linealizado con el mismo controlador en cascada, lo justo para que las trazas
reaccionen a los parámetros de forma creíble. Los rangos salen de
demo_schema.json, capturado del firmware real con el comando "schema".
"""
from __future__ import annotations

import asyncio
import json
import math
import random
from pathlib import Path

from .base import CloseHandler, LineHandler, Transport
from .demo_bench import ENC, GET_MAX_ROWS, SimBench
from .demo_cascade import SimCascade

SCHEMA = json.loads((Path(__file__).with_name("demo_schema.json")).read_text(encoding="utf-8"))
PERIOD = 0.02
FW = "demo"

# Dinámica en grados: θ'' = A·θ + B·u + perturbación. Con los valores de fábrica el lazo
# interno deja θ ≈ 1.1·θref, así que la ganancia del lazo de velocidad queda cerca de la
# del robot real (≈ 15 RPM/° · kp_v).
A = 50.0
B = 8.0
DIST_STD = 25.0      # perturbación de par, constante durante cada período [°/s²]
PICKUP_S = 1.5       # tras una caída, "alguien lo levanta" a los 1.5 s


class SimRobot:
    """Estado y paso de simulación, sin tiempo real (se prueba en tests/test_demo_sim.py)."""

    def __init__(self, params: dict):
        self.th, self.om = 3.0, 0.0  # ángulo [°], velocidad angular [°/s]
        self.rpm = 0.0
        self.ref = 0.0
        self.kp = params["kp_min"]
        self.active = False
        self.down_time = PICKUP_S    # s en el suelo
        self._speed_int = 0.0
        self._outer_acc, self._outer_n = 0.0, 0

    def step(self, p: dict, estop: bool, dt: float = PERIOD) -> tuple[dict, bool]:
        """Avanza un período de control. Devuelve (campos de la trama, se cayó en este paso)."""
        u = up = ud = 0.0
        if estop and self.active:
            self.active = False
        if not self.active:
            self.down_time += dt
            if not estop and self.down_time >= PICKUP_S:
                self.th, self.om, self.rpm = random.uniform(-3, 3), 0.0, 0.0
            if not estop and abs(self.th) < p["rearm_angle"]:
                self.active = True
                self.ref = p["setpoint_angle"]
                self._speed_int, self._outer_acc, self._outer_n = 0.0, 0.0, 0
        if self.active:
            if p["structure"] == "SpeedOuter":
                self._outer_acc += self.rpm
                self._outer_n += 1
                if self._outer_n >= 5:
                    v = self._outer_acc / self._outer_n - p["speed_ref"]
                    self._outer_acc, self._outer_n = 0.0, 0
                    self._speed_int += v * 0.1
                    tilt = p["speed_sign"] * (p["kp_v"] * v + p["ki_v"] * self._speed_int)
                    self.ref = p["setpoint_angle"] + max(-p["max_tilt_ref"], min(p["max_tilt_ref"], tilt))
            else:
                self.ref = p["setpoint_angle"]
            target = p["kp_min"] + (p["kp_max"] - p["kp_min"]) * min(1.0, abs(self.ref - self.th) / 5)
            self.kp += dt / (p["kp_tau"] + dt) * (target - self.kp)
            up = 0.8 * self.kp * (self.ref - self.th)
            ud = 0.8 * p["kd_angle"] * (-self.om)
            u = max(-255.0, min(255.0, up + ud))
        db = p["pwm_deadband"]
        um = 0.0 if u == 0 else math.copysign(db + abs(u) * (255 - db) / 255, u)

        fell = False
        if self.active:
            dist = random.gauss(0, DIST_STD)
            for _ in range(10):
                h = dt / 10
                self.om += (A * self.th + B * u + dist) * h
                self.th += self.om * h
            # Como en el robot real: en régimen avanza ≈ 15 RPM por grado de inclinación
            # (README, "Por qué el signo del lazo de velocidad es −1")
            self.rpm += (15.0 * self.th - self.rpm) * dt / 0.3
            if abs(self.th) > p["max_angle"]:
                self.active, fell, self.down_time = False, True, 0.0
                self.th = math.copysign(60.0, self.th)  # acostado
                self.om = self.rpm = 0.0

        frame = {
            "ang": round(self.th + random.gauss(0, 0.05), 2), "ref": round(self.ref, 2),
            "w": round(self.om + random.gauss(0, 1.5), 1),
            "pwm": round(u, 1), "pwmM": round(um, 1),
            "rpmL": round(self.rpm + random.gauss(0, 0.8), 1), "rpmR": round(self.rpm + random.gauss(0, 0.5), 1),
            "kp": round(self.kp, 2), "dt": PERIOD, "uP": round(up, 1), "uI": 0.0, "uD": round(ud, 1),
            "st": "ACTIVE" if self.active else ("ESTOP" if estop else "IDLE"),
        }
        return frame, fell


class DemoTransport(Transport):
    kind = "demo"

    def __init__(self):
        self.params = {d["key"]: d["def"] for d in SCHEMA}
        self.tel = {"on": True, "json": False, "div": 5}
        self.estop = False
        self.wifi = {"mode": "ap", "active": "ap", "hostname": "balancin-demo.local", "ap_ssid": "Balancin-DEMO",
                     "ap_pass": "bal-000000", "ap_pass_default": True, "ssid": "", "pass_set": False,
                     "ip": "192.168.4.1", "clients": 0}
        self.mqtt = {"enabled": False, "host": "", "port": 1883, "connected": False, "topic": "balancin/balancin-demo"}
        self.bench = SimBench()
        self.casc = SimCascade()  # estructura RpmCascade (M3/M4)
        self._task: asyncio.Task | None = None
        self._on_line: LineHandler | None = None

    @property
    def target(self) -> str:
        return "robot simulado"

    async def open(self, on_line: LineHandler, on_close: CloseHandler) -> None:
        self._on_line = on_line
        self._task = asyncio.create_task(self._run())

    async def close(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        self._task = None

    # ------------------------------------------------------------------ salida
    def _emit(self, obj_or_text):
        if self._on_line is None:
            return
        line = obj_or_text if isinstance(obj_or_text, str) else json.dumps(obj_or_text, ensure_ascii=False)
        self._on_line(line)

    def _hello(self):
        self._emit({"type": "hello", "fw": FW, "proto": 1, "period_ms": 20,
                    "structure": self.params["structure"], "params_src": "factory", "enc": ENC, "bench": True})

    # ------------------------------------------------------------------ simulación
    async def _run(self):
        self._emit("Robot simulado listo (transporte demo).")
        self._hello()
        loop = asyncio.get_running_loop()
        sim = SimRobot(self.params)
        seq = 0
        t0 = next_t = loop.time()
        reported = (0, 0)
        while True:
            next_t += PERIOD
            if self.bench.on:
                self._bench_period(loop.time() - t0)
                if self.bench.manual_expired:
                    self.bench.manual_expired = False
                    self._emit(">> Banco: PWM manual apagado (no se renovó en 30 s).")
                b = self.bench
                if b.cap_state in (2, 3) and (b.cap_id, b.cap_state) != reported:
                    reported = (b.cap_id, b.cap_state)
                    self._emit(f">> Banco: ensayo {b.cap_id} {'terminado' if b.cap_state == 2 else 'abortado'} "
                               f"({len(b.cap_rows)} muestras).")
                await asyncio.sleep(max(0.0, next_t - loop.time()))
                continue
            self.bench.idle(int(PERIOD * 1000))
            if self.params["structure"] == "RpmCascade":
                fields, fell = self.casc.step(self.params, self.estop)
            else:
                fields, fell = sim.step(self.params, self.estop)
            if fell:
                self._emit(">> Robot caído (simulado).")
            frame = {"type": "tel", "seq": seq, "t": int((loop.time() - t0) * 1000), **fields}
            if self.tel["on"] and seq % self.tel["div"] == 0:
                if self.tel["json"]:
                    self._emit(frame)
                else:
                    self._emit(f"Ang: {frame['ang']:.2f} | Ref: {frame['ref']:.2f} | PWM: {frame['pwm']:.1f} | demo")
            seq += 1
            await asyncio.sleep(max(0.0, next_t - loop.time()))

    def _bench_period(self, t_s: float):
        for _ in range(int(PERIOD * 1000)):
            f = self.bench.tick(self.estop)
            if f is None:
                continue
            f["t"] = int(t_s * 1000)
            if self.tel["on"] and f["seq"] % self.tel["div"] == 0:
                if self.tel["json"]:
                    self._emit(f)
                else:
                    self._emit(f"Banco | PWM L {f['pwmL']} R {f['pwmR']} | RPM L {f['rpmL']:.1f} R {f['rpmR']:.1f} | demo")

    def _bench_cmd(self, mid, msg: dict):
        b, op = self.bench, msg.get("op", "status")
        if op == "status":
            pass
        elif op == "on":
            b.enter()
            self._emit(">> Banco de motores activo: control en pausa. El robot debe estar sobre un soporte.")
        elif op == "off":
            if b.on:
                b.exit()
                self.estop = True
                self._emit(">> Banco de motores cerrado. Parada de emergencia activa: Armar para volver al control.")
        elif op == "stop":
            b.stop()
        elif op == "pwm":
            l, r = msg.get("l", b.manual[0]), msg.get("r", b.manual[1])
            if not all(isinstance(v, int) and not isinstance(v, bool) for v in (l, r)):
                return self._ack(mid, False, "l y r deben ser enteros")
            err = b.set_pwm(l, r, self.estop)
            if err:
                return self._ack(mid, False, err)
        elif op == "rpm":
            r = msg.get("r")
            if isinstance(r, bool) or not isinstance(r, (int, float)):
                return self._ack(mid, False, "r (RPM) debe ser un número")
            err = b.set_rpm(r, self.estop)
            if err:
                return self._ack(mid, False, err)
        elif op == "rpmcfg":
            err = b.set_cfg({k: v for k, v in msg.items() if k not in ("type", "id", "cmd", "op")})
            if err:
                return self._ack(mid, False, err)
        elif op == "run":
            err = b.run(msg.get("motor", ""), msg.get("seq"), msg.get("period_ms", 2), self.estop,
                        msg.get("kind", "pwm"))
            if err:
                return self._ack(mid, False, err)
        elif op == "get":
            frm, count = msg.get("from", 0), msg.get("count", 100)
            if not (isinstance(frm, int) and isinstance(count, int) and frm >= 0 and 1 <= count <= GET_MAX_ROWS):
                return self._ack(mid, False, f"from ≥ 0 y count en [1, {GET_MAX_ROWS}]")
            n = b.readable()
            if not n:
                return self._ack(mid, False, "no hay un ensayo terminado para leer")
            return self._ack(mid, True, cap=b.cap_id, total=n, **{"from": frm}, rows=b.cap_rows[frm:frm + count])
        else:
            return self._ack(mid, False, f"op desconocida: {op}")
        self._ack(mid, True, bench=b.status(self.estop))

    # ------------------------------------------------------------------ comandos
    async def send(self, line: str) -> None:
        line = line.strip()
        if not line.startswith("{"):
            return
        try:
            msg = json.loads(line)
        except json.JSONDecodeError as e:
            return self._ack(None, False, f"JSON inválido: {e}")
        mid, cmd = msg.get("id"), msg.get("cmd")
        if msg.get("type") != "cmd":
            return self._ack(mid, False, 'type debe ser "cmd"')
        if cmd == "hello":
            self._hello()
            return self._ack(mid, True)
        if cmd == "get":
            return self._ack(mid, True, params=dict(self.params))
        if cmd == "schema":
            return self._ack(mid, True, schema=SCHEMA)
        if cmd == "set":
            if not isinstance(msg.get("params"), dict):
                return self._ack(mid, False, "set requiere un objeto params")
            err = self._apply(msg["params"])
            return self._ack(mid, False, err) if err else self._ack(mid, True, params=dict(self.params))
        if cmd == "defaults":
            self.params = {d["key"]: d["def"] for d in SCHEMA}
            return self._ack(mid, True, params=dict(self.params))
        if cmd == "bench":
            return self._bench_cmd(mid, msg)
        if cmd == "drive":
            if msg.get("home"):
                self.casc.home()
            else:
                v, r = msg.get("v", 0), msg.get("r", 0)
                if any(isinstance(x, bool) or not isinstance(x, (int, float)) for x in (v, r)):
                    return self._ack(mid, False, "v (RPM) y r (°/s) deben ser números")
                self.casc.drive(v, r)
            c = self.casc
            return self._ack(mid, True, drive={"v": c.v_cmd, "r": c.r_cmd, "home": c.homing})
        if cmd in ("calib", "deadband") and self.bench.on:
            return self._ack(mid, False, "el banco de motores está activo: salir del banco primero")
        if cmd in ("save", "calib", "deadband"):
            self._emit(f">> {cmd} (simulado)")
            await asyncio.sleep(0.3)
            return self._ack(mid, True)
        if cmd in ("estop", "arm"):
            self.estop = cmd == "estop"
            return self._ack(mid, True)
        if cmd == "wifi":
            new = msg.get("set")
            if isinstance(new, dict):
                mode = new.get("mode", self.wifi["mode"])
                if mode not in ("ap", "sta", "off"):
                    return self._ack(mid, False, 'mode debe ser "ap", "sta" u "off"')
                if mode == "sta" and not (new.get("ssid") or self.wifi["ssid"]):
                    return self._ack(mid, False, "ssid: entre 1 y 32 caracteres")
                self.wifi.update(mode=mode, active=mode, ssid=new.get("ssid", self.wifi["ssid"]),
                                 pass_set=bool(new.get("pass")) or self.wifi["pass_set"])
            return self._ack(mid, True, wifi=dict(self.wifi))
        if cmd == "mqtt":
            new = msg.get("set")
            if isinstance(new, dict):
                if new.get("enabled") and not (new.get("host") or self.mqtt["host"]):
                    return self._ack(mid, False, "host: IP o nombre del broker (1 a 63 caracteres)")
                self.mqtt.update({k: new[k] for k in ("enabled", "host", "port") if k in new})
                self.mqtt["connected"] = False  # el simulado no tiene broker
            return self._ack(mid, True, mqtt=dict(self.mqtt))
        if cmd == "tel":
            if "on" in msg:
                self.tel["on"] = bool(msg["on"])
            if "fmt" in msg:
                self.tel["json"] = msg["fmt"] == "json"
            if "div" in msg:
                self.tel["div"] = int(msg["div"])
            return self._ack(mid, True, on=self.tel["on"], fmt="json" if self.tel["json"] else "text",
                             div=self.tel["div"], dropped=0)
        self._ack(mid, False, f"comando desconocido: {cmd}")

    def _ack(self, mid, ok, err=None, **extra):
        msg = {"type": "ack", "id": mid, "ok": ok, **extra}
        if not ok:
            msg["err"] = err
        self._emit(msg)

    def _apply(self, changes: dict) -> str | None:
        by_key = {d["key"]: d for d in SCHEMA}
        cand = dict(self.params)
        for k, v in changes.items():
            d = by_key.get(k)
            if d is None:
                return f"parámetro desconocido: {k}"
            t = d["type"]
            if t == "enum":
                if v not in d["options"]:
                    return f"{k} debe ser uno de {d['options']}"
            elif t == "bool":
                if not isinstance(v, bool):
                    return f"{k} debe ser true o false"
            else:
                if isinstance(v, bool) or not isinstance(v, (int, float)):
                    return f"{k} debe ser numérico"
                if t == "int" and not float(v).is_integer():
                    return f"{k} debe ser entero"
                if t == "sign" and v not in (-1, 1):
                    return f"{k} debe ser -1 o 1"
                if not d["min"] <= v <= d["max"]:
                    return f"{k} fuera de rango [{d['min']:.3f}, {d['max']:.3f}]"
            cand[k] = v
        if cand["kp_min"] > cand["kp_max"]:
            return "kp_min debe ser <= kp_max"
        if cand["rearm_angle"] >= cand["max_angle"]:
            return "rearm_angle debe ser < max_angle"
        self.params = cand
        return None
