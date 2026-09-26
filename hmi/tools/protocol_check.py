#!/usr/bin/env python3
"""Verificación del protocolo v1 contra el robot conectado por USB.

Uso:
    python3 hmi/tools/protocol_check.py [--port /dev/cu.wchusbserial...] [--save]

Comprueba hello, schema, get, set válido e inválido, defaults, estop/arm y la
telemetría JSON a 50 Hz (frecuencia real, huecos de seq y dt). No mueve el robot:
activa la parada de emergencia al empezar y la libera al final.

Con --save además guarda los parámetros en NVS y pide reiniciar el robot para
comprobar que persisten (criterio de aceptación de F0).

Al terminar deja los parámetros como estaban al empezar (y, con --save, los vuelve
a guardar en NVS).

Requiere pyserial (incluido en el entorno de PlatformIO:
~/.platformio/penv/bin/python hmi/tools/protocol_check.py).
"""
import argparse
import glob
import json
import sys
import time

import serial

BAUD = 921600


class Robot:
    def __init__(self, port):
        self.ser = serial.Serial(port, BAUD, timeout=0.05)
        self.next_id = 1
        self.pending = []  # mensajes JSON recibidos que no son la respuesta esperada

    def lines(self, seconds):
        """Devuelve (mensajes JSON, líneas de texto) recibidos durante `seconds`."""
        msgs, text = [], []
        end = time.time() + seconds
        while time.time() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("{"):
                try:
                    msgs.append(json.loads(line))
                except json.JSONDecodeError:
                    text.append(line)
            elif line:
                text.append(line)
        return msgs, text

    def cmd(self, cmd, timeout=3.0, **fields):
        mid = self.next_id
        self.next_id += 1
        msg = {"type": "cmd", "id": mid, "cmd": cmd, **fields}
        self.ser.write((json.dumps(msg) + "\n").encode())
        end = time.time() + timeout
        while time.time() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("{"):
                continue
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if m.get("type") == "ack" and m.get("id") == mid:
                return m
            self.pending.append(m)
        raise TimeoutError(f"sin respuesta a {cmd}")


results = []


def check(name, ok, detail=""):
    results.append(ok)
    print(f"  [{'OK' if ok else 'FALLA'}] {name}" + (f"  ({detail})" if detail else ""))


def find_port():
    ports = glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")
    return ports[0] if ports else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=find_port())
    ap.add_argument("--save", action="store_true", help="probar persistencia en NVS (pide reiniciar)")
    args = ap.parse_args()
    if not args.port:
        sys.exit("No se encontró el puerto. Usar --port.")

    r = Robot(args.port)
    r.lines(0.5)
    r.cmd("tel", on=False)
    r.lines(0.3)

    print("Conexión")
    ack = r.cmd("hello")
    hello = next((m for m in r.pending if m.get("type") == "hello"), None)
    check("hello", ack["ok"] and hello is not None, hello and f"fw {hello['fw']}, proto {hello['proto']}")

    print("Parada de emergencia")
    check("estop", r.cmd("estop")["ok"])

    print("Parámetros")
    schema = r.cmd("schema")
    keys = [d["key"] for d in schema.get("schema", [])]
    check("schema", schema["ok"] and len(keys) > 0, f"{len(keys)} parámetros")
    params = r.cmd("get")["params"]
    initial = dict(params)
    check("get devuelve todo el schema", set(params) == set(keys))

    original = params["kd_angle"]
    new = round(original + 0.05, 3)
    a = r.cmd("set", params={"kd_angle": new})
    check("set válido", a["ok"] and abs(a["params"]["kd_angle"] - new) < 1e-4, f"kd_angle {original} → {new}")
    a = r.cmd("set", params={"kd_angle": 99})
    check("set fuera de rango se rechaza", not a["ok"], a.get("err", ""))
    a = r.cmd("set", params={"kp_min": 80, "kp_max": 60})
    check("regla kp_min <= kp_max", not a["ok"], a.get("err", ""))
    a = r.cmd("set", params={"no_existe": 1})
    check("clave desconocida se rechaza", not a["ok"], a.get("err", ""))
    a = r.cmd("set", params={"kd_angle": 2.0, "kp_max": -1})
    after = r.cmd("get")["params"]["kd_angle"]
    check("set es todo o nada", not a["ok"] and abs(after - new) < 1e-4)
    a = r.cmd("set", params={"structure": "AngleOuter"})
    check("cambio de estructura", a["ok"] and a["params"]["structure"] == "AngleOuter")
    a = r.cmd("defaults")
    check("defaults", a["ok"] and all(a["params"][d["key"]] == d["def"] for d in schema["schema"]))

    print("Telemetría")
    r.cmd("tel", on=True, fmt="json", div=1)
    msgs, _ = r.lines(3.0)
    tel = [m for m in msgs if m.get("type") == "tel"]
    if len(tel) >= 2:
        span = (tel[-1]["t"] - tel[0]["t"]) / 1000
        rate = (len(tel) - 1) / span if span > 0 else 0
        gaps = sum(1 for a, b in zip(tel, tel[1:]) if b["seq"] - a["seq"] != 1)
        dts = [m["dt"] for m in tel]
        check("frecuencia ≈ 50 Hz", 45 <= rate <= 55, f"{rate:.1f} Hz")
        check("sin huecos en seq", gaps == 0, f"{gaps} huecos en {len(tel)} tramas")
        check("dt estable", max(dts) < 0.025, f"dt {min(dts):.4f}…{max(dts):.4f} s")
        check("estado ESTOP", all(m["st"] == "ESTOP" for m in tel))
    else:
        check("llega telemetría", False)
    r.cmd("tel", fmt="text", div=5, on=False)

    if args.save:
        print("Persistencia en NVS")
        r.cmd("set", params={"kd_angle": 1.3})
        check("save", r.cmd("save", timeout=5)["ok"])
        input("  Reinicia el robot (botón EN/RST) y pulsa Enter...")
        r.lines(2.0)
        r.cmd("estop")
        kd = r.cmd("get")["params"]["kd_angle"]
        check("kd_angle persiste tras reiniciar", abs(kd - 1.3) < 1e-4, f"kd_angle = {kd}")

    check("restaurar parámetros iniciales", r.cmd("set", params=initial)["ok"])
    if args.save:
        check("guardar parámetros iniciales en NVS", r.cmd("save", timeout=5)["ok"])

    check("arm", r.cmd("arm")["ok"])
    r.cmd("tel", on=True)

    print(f"\n{sum(results)}/{len(results)} comprobaciones correctas")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
