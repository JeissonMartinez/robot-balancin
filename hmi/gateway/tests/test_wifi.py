"""Transporte WiFi contra un robot simulado servido por WebSocket (como el del ESP32)."""
import asyncio
import threading
import time

import pytest
import websockets
from fastapi.testclient import TestClient

from app.main import create_app
from app.transports.demo import DemoTransport


class FakeRobotServer:
    """Servidor WebSocket en un hilo: cada cliente habla con su propio DemoTransport."""

    def __init__(self):
        self.port = None
        self.loop = asyncio.new_event_loop()
        self.server = None
        self.ready = threading.Event()
        threading.Thread(target=self._run, daemon=True).start()
        self.ready.wait(5)

    def _run(self):
        asyncio.set_event_loop(self.loop)

        async def handler(ws):
            demo = DemoTransport()
            demo.tel = {"on": True, "json": True, "div": 1}  # como el canal WiFi del firmware
            out = asyncio.Queue()
            await demo.open(out.put_nowait, lambda r: None)

            async def pump():
                while True:
                    await ws.send(await out.get())

            task = asyncio.create_task(pump())
            try:
                async for msg in ws:
                    await demo.send(msg)
            except websockets.ConnectionClosed:
                pass
            finally:
                task.cancel()
                await demo.close()

        async def main():
            self.server = await websockets.serve(handler, "127.0.0.1", 0)
            self.port = self.server.sockets[0].getsockname()[1]
            self.ready.set()
            await self.server.wait_closed()

        self.loop.run_until_complete(main())

    def stop(self):
        self.loop.call_soon_threadsafe(self.server.close)


@pytest.fixture
def robot():
    srv = FakeRobotServer()
    yield srv
    srv.stop()


@pytest.fixture
def client(tmp_path):
    with TestClient(create_app(db_path=tmp_path / "t.db", web_dist=tmp_path / "x")) as c:
        yield c


def test_wifi_connect_stream_and_wifi_cmd(client, robot):
    r = client.post("/api/connect", json={"transport": "wifi", "host": f"127.0.0.1:{robot.port}"})
    assert r.status_code == 200, r.text
    st = r.json()
    assert st["transport"] == "wifi" and st["target"] == f"ws://127.0.0.1:{robot.port}/ws"
    time.sleep(1.2)
    assert client.get("/api/status").json()["rate"] > 30

    with client.websocket_connect("/ws") as ws:
        ws.receive_json()
        ws.send_json({"type": "cmd", "id": 1, "cmd": "wifi"})
        while (m := ws.receive_json())["type"] != "ack":
            pass
        assert m["ok"] and m["wifi"]["ap_ssid"] == "Balancin-DEMO"
    client.post("/api/disconnect")


def test_wifi_unreachable(client):
    r = client.post("/api/connect", json={"transport": "wifi", "host": "127.0.0.1:1"})
    assert r.status_code == 502 and "no se pudo abrir" in r.json()["detail"]
    assert client.get("/api/status").json()["retrying"] is False


def test_wifi_reconnects_after_loss(client, robot):
    client.post("/api/connect", json={"transport": "wifi", "host": f"127.0.0.1:{robot.port}"})
    first = client.get("/api/status").json()["session_id"]
    link = client.app.state.link
    # Simula la pérdida del enlace: se cierra el socket desde el lado del gateway
    client.portal.call(lambda: link.transport._ws.close())
    end = time.time() + 8
    while time.time() < end:
        st = client.get("/api/status").json()
        if st["state"] == "connected" and st["session_id"] != first:
            break
        time.sleep(0.2)
    assert st["state"] == "connected" and st["session_id"] != first
    client.post("/api/disconnect")
    assert client.get("/api/status").json()["retrying"] is False
