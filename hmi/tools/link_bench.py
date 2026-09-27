#!/usr/bin/env python3
"""Mide un enlace robot ↔ PC (F5: comparar WiFi directo con MQTT).

Uso (con el gateway DESCONECTADO del robot, para no interferir):
    hmi/gateway/.venv/bin/python hmi/tools/link_bench.py --serial /dev/cu.wchusbserial...
    hmi/gateway/.venv/bin/python hmi/tools/link_bench.py --ws 192.168.4.1
    hmi/gateway/.venv/bin/python hmi/tools/link_bench.py --mqtt 192.168.1.7 --robot balancin-b884

Mide:
- Ida y vuelta de un comando (n veces, uno tras otro): lo que tarda la HMI en ver la
  respuesta a un cambio. El comando es "tel" sin campos: responde sin cambiar nada.
- Telemetría durante --segundos a 50 Hz: frecuencia recibida, tramas perdidas (huecos en
  seq) y jitter de llegada: cuánto se aparta el intervalo entre llegadas al PC del
  intervalo entre muestras en el robot (retrasos y ráfagas del enlace).

Imprime una tabla y una fila Markdown para docs/hmi/PLAN.md.
"""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from protocol_check import SerialLink, WsLink  # noqa: E402


class MqttLink:
    def __init__(self, broker, robot, port=1883):
        import queue

        import paho.mqtt.client as mqtt

        self.q = queue.Queue()
        base = f"balancin/{robot}"
        self.t_in = f"{base}/in"
        self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"bench-{int(time.time())}")
        self.c.on_message = lambda _c, _u, m: self.q.put(m.payload.decode("utf-8", "replace"))
        self.c.connect(broker, port, 10)
        self.c.subscribe(f"{base}/out")
        self.c.loop_start()
        time.sleep(0.5)

    def readline(self):
        import queue

        try:
            return self.q.get(timeout=0.05)
        except queue.Empty:
            return ""

    def write(self, line):
        self.c.publish(self.t_in, line)


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p / 100 * (len(xs) - 1))))] if xs else float("nan")


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--serial", metavar="PUERTO")
    g.add_argument("--ws", metavar="HOST")
    g.add_argument("--mqtt", metavar="BROKER")
    ap.add_argument("--robot", help="con --mqtt: nombre del robot (balancin-xxxx)")
    ap.add_argument("-n", type=int, default=100, help="comandos para medir ida y vuelta")
    ap.add_argument("--segundos", type=float, default=20)
    args = ap.parse_args()

    if args.serial:
        link, name = SerialLink(args.serial), "USB (Serial 921600)"
    elif args.ws:
        link, name = WsLink(args.ws), f"WiFi WebSocket ({args.ws})"
    else:
        if not args.robot:
            sys.exit("--mqtt requiere --robot")
        link, name = MqttLink(args.mqtt, args.robot), f"MQTT ({args.mqtt})"

    def drain(seconds):
        end = time.time() + seconds
        while time.time() < end:
            link.readline()

    def command(cid, cmd, timeout=3.0, **fields):
        link.write(json.dumps({"type": "cmd", "id": cid, "cmd": cmd, **fields}))
        end = time.time() + timeout
        while time.time() < end:
            line = link.readline().strip()
            if line.startswith("{"):
                try:
                    m = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if m.get("type") == "ack" and m.get("id") == cid:
                    return m
        return None

    # Sin telemetría para medir la ida y vuelta limpia
    command(1, "tel", on=False)
    drain(0.5)

    print(f"Enlace: {name}")
    rtts, lost = [], 0
    for i in range(args.n):
        t0 = time.perf_counter()
        if command(1000 + i, "tel") is None:
            lost += 1
        else:
            rtts.append((time.perf_counter() - t0) * 1000)
    print(f"  ida y vuelta ({len(rtts)}/{args.n}): p50 {pct(rtts, 50):.1f} ms · p95 {pct(rtts, 95):.1f} ms · máx {max(rtts, default=float('nan')):.1f} ms")

    command(2, "tel", on=True, fmt="json", div=1)
    frames = []
    end = time.time() + args.segundos
    while time.time() < end:
        line = link.readline().strip()
        if line.startswith("{"):
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if m.get("type") == "tel":
                frames.append((time.perf_counter(), m["seq"], m["t"]))

    # Dejar el canal como se usa normalmente
    if args.serial:
        command(3, "tel", fmt="text", div=5, on=True)
    elif args.mqtt:
        command(3, "tel", on=False)

    if len(frames) < 2:
        print("  telemetría: no llegaron tramas")
        return
    span = frames[-1][0] - frames[0][0]
    rate = (len(frames) - 1) / span
    gaps = sum(b[1] - a[1] - 1 for a, b in zip(frames, frames[1:]) if b[1] > a[1] + 1)
    expected = frames[-1][1] - frames[0][1] + 1
    jitter = [abs((b[0] - a[0]) * 1000 - (b[2] - a[2])) for a, b in zip(frames, frames[1:])]
    print(f"  telemetría: {rate:.1f} Hz · perdidas {gaps}/{expected} ({100 * gaps / expected:.2f} %)")
    print(f"  jitter de llegada: p50 {pct(jitter, 50):.1f} ms · p95 {pct(jitter, 95):.1f} ms · máx {max(jitter):.1f} ms")
    print()
    print("Fila para PLAN.md:")
    print(f"| {name} | {pct(rtts, 50):.1f} / {pct(rtts, 95):.1f} | {rate:.1f} | {100 * gaps / expected:.2f} % "
          f"| {pct(jitter, 95):.1f} / {max(jitter):.1f} |")


if __name__ == "__main__":
    main()
