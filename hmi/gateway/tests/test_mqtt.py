"""Transporte MQTT contra un Mosquitto local y un robot simulado que publica como el firmware.

Se omiten si `mosquitto` no está instalado (brew install mosquitto / apt install mosquitto).
"""
import asyncio
import shutil
import socket
import subprocess
import threading
import time

import paho.mqtt.client as mqtt
import pytest
from fastapi.testclient import TestClient

from app.main import create_app
from app.transports.demo import DemoTransport

MOSQUITTO = shutil.which("mosquitto") or next(
    (p for p in ("/opt/homebrew/sbin/mosquitto", "/usr/local/sbin/mosquitto", "/usr/sbin/mosquitto") if shutil.os.path.exists(p)), None)
pytestmark = pytest.mark.skipif(MOSQUITTO is None, reason="mosquitto no instalado")
ROBOT = "balancin-test"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture
def broker(tmp_path):
    port = free_port()
    conf = tmp_path / "m.conf"
    conf.write_text(f"listener {port} 127.0.0.1\nallow_anonymous true\n")
    proc = subprocess.Popen([MOSQUITTO, "-c", str(conf)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), 0.1).close()
            break
        except OSError:
            time.sleep(0.05)
    yield port
    proc.terminate()
    proc.wait(3)


class FakeMqttRobot:
    """Como mqtt_link.cpp: in/out/status con testamento, telemetría apagada al inicio."""

    def __init__(self, port):
        base = f"balancin/{ROBOT}"
        self.t_in, self.t_out, self.t_status = f"{base}/in", f"{base}/out", f"{base}/status"
        self.loop = asyncio.new_event_loop()
        self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=ROBOT)
        self.c.will_set(self.t_status, "offline", qos=1, retain=True)
        self.c.on_message = lambda _c, _u, m: asyncio.run_coroutine_threadsafe(
            self.demo.send(m.payload.decode()), self.loop)
        self.c.connect("127.0.0.1", port)
        self.c.subscribe(self.t_in)
        self.c.loop_start()
        ready = threading.Event()
        threading.Thread(target=self._run, args=(ready,), daemon=True).start()
        ready.wait(5)
        self.c.publish(self.t_status, "online", qos=1, retain=True)

    def _run(self, ready):
        asyncio.set_event_loop(self.loop)
        self.demo = DemoTransport()
        self.demo.tel = {"on": False, "json": True, "div": 1}

        async def main():
            await self.demo.open(lambda line: self.c.publish(self.t_out, line), lambda r: None)
            ready.set()
            while True:
                await asyncio.sleep(1)

        self.loop.run_until_complete(main())

    def die(self):
        """Corte brusco: el broker publica el testamento "offline"."""
        self.c.socket().close()


@pytest.fixture
def client(tmp_path):
    with TestClient(create_app(db_path=tmp_path / "t.db", web_dist=tmp_path / "x")) as c:
        yield c


def test_discover_and_connect(client, broker):
    FakeMqttRobot(broker)
    robots = client.get(f"/api/mqtt/robots?broker=127.0.0.1&port={broker}").json()
    assert {"robot": ROBOT, "status": "online"} in robots

    r = client.post("/api/connect", json={"transport": "mqtt", "host": "127.0.0.1", "mqtt_port": broker, "robot": ROBOT})
    assert r.status_code == 200, r.text
    assert r.json()["transport"] == "mqtt"
    time.sleep(1.2)
    assert client.get("/api/status").json()["rate"] > 30
    client.post("/api/disconnect")


def test_unknown_robot(client, broker):
    r = client.post("/api/connect", json={"transport": "mqtt", "host": "127.0.0.1", "mqtt_port": broker, "robot": "no-existe"})
    assert r.status_code == 502 and "no está en el broker" in r.json()["detail"]


def test_robot_offline_is_connection_lost(client, broker):
    robot = FakeMqttRobot(broker)
    client.post("/api/connect", json={"transport": "mqtt", "host": "127.0.0.1", "mqtt_port": broker, "robot": ROBOT})
    robot.die()
    end = time.time() + 30  # al cerrarse el socket el broker publica el testamento
    while time.time() < end and client.get("/api/status").json()["state"] == "connected":
        time.sleep(0.2)
    st = client.get("/api/status").json()
    assert st["state"] != "connected" and st["retrying"]
    client.post("/api/disconnect")
