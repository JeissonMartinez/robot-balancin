"""Transporte MQTT: el robot y el gateway se hablan a través de un broker (Mosquitto).

Temas (ver include/mqtt_link.h del firmware):
    balancin/<robot>/in      gateway → robot (comandos)
    balancin/<robot>/out     robot → gateway (ack, hello, logs, telemetría)
    balancin/<robot>/status  "online" / "offline", retenido (testamento del robot)

Cada mensaje es una línea del protocolo, así que RobotLink no distingue el transporte.
paho-mqtt corre su propio hilo; cada mensaje se entrega al event loop con
call_soon_threadsafe, como en el transporte serie.
"""
from __future__ import annotations

import asyncio
import threading
import time
import uuid

import paho.mqtt.client as mqtt

from .base import CloseHandler, LineHandler, Transport

DEFAULT_BROKER = "127.0.0.1"
DEFAULT_PORT = 1883
PREFIX = "balancin"


def _client(name: str) -> mqtt.Client:
    return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"{name}-{uuid.uuid4().hex[:6]}")


def discover_robots(broker: str, port: int = DEFAULT_PORT, wait_s: float = 1.2) -> list[dict]:
    """Robots que anunciaron su estado en el broker (mensajes retenidos de .../status)."""
    found: dict[str, str] = {}
    ready = threading.Event()
    c = _client("gateway-busqueda")
    c.on_connect = lambda cl, *_: (cl.subscribe(f"{PREFIX}/+/status"), ready.set())
    c.on_message = lambda _c, _u, m: found.__setitem__(m.topic.split("/")[1], m.payload.decode(errors="replace"))
    try:
        c.connect(broker, port, keepalive=10)
    except OSError as e:
        raise ConnectionError(f"broker {broker}:{port}: {e}") from None
    c.loop_start()
    try:
        if not ready.wait(3):
            raise ConnectionError(f"broker {broker}:{port} no respondió")
        time.sleep(wait_s)
    finally:
        c.loop_stop()
        c.disconnect()
    return [{"robot": r, "status": s} for r, s in sorted(found.items())]


class MqttTransport(Transport):
    kind = "mqtt"

    def __init__(self, broker: str, robot: str, port: int = DEFAULT_PORT):
        self.broker = broker
        self.port = port
        self.robot = robot
        base = f"{PREFIX}/{robot}"
        self.t_in, self.t_out, self.t_status = f"{base}/in", f"{base}/out", f"{base}/status"
        self._c: mqtt.Client | None = None
        self._closing = False

    @property
    def target(self) -> str:
        return f"mqtt://{self.broker}:{self.port}/{PREFIX}/{self.robot}"

    async def open(self, on_line: LineHandler, on_close: CloseHandler) -> None:
        loop = asyncio.get_running_loop()
        self._closing = False
        connected = asyncio.Event()
        status: asyncio.Future = loop.create_future()

        def set_status(s: str):
            if not status.done():
                status.set_result(s)
            elif s == "offline" and not self._closing:
                on_close(f"MQTT: el robot {self.robot} se desconectó del broker")

        def on_connect(cl, _u, _f, rc, _p):
            if rc.is_failure:
                return
            cl.subscribe([(self.t_out, 0), (self.t_status, 1)])
            loop.call_soon_threadsafe(connected.set)

        def on_message(_c, _u, m):
            text = m.payload.decode("utf-8", "replace")
            if m.topic == self.t_status:
                loop.call_soon_threadsafe(set_status, text)
            else:
                for line in text.splitlines() or [""]:
                    loop.call_soon_threadsafe(on_line, line)

        def on_disconnect(_c, _u, _f, rc, _p):
            if not self._closing:
                loop.call_soon_threadsafe(on_close, f"MQTT: se perdió el broker ({rc})")

        c = _client("gateway")
        c.on_connect, c.on_message, c.on_disconnect = on_connect, on_message, on_disconnect
        try:
            await asyncio.to_thread(c.connect, self.broker, self.port, 10)
        except OSError as e:
            raise ConnectionError(f"no se pudo abrir el broker {self.broker}:{self.port}: {e}") from None
        c.loop_start()
        self._c = c
        try:
            await asyncio.wait_for(connected.wait(), 5)
            # El estado retenido llega enseguida si el robot se anunció alguna vez
            s = await asyncio.wait_for(status, 2)
        except asyncio.TimeoutError:
            await self.close()
            raise ConnectionError(f"el robot {self.robot} no está en el broker {self.broker}") from None
        if s != "online":
            await self.close()
            raise ConnectionError(f"el robot {self.robot} está desconectado del broker")

    async def send(self, line: str) -> None:
        if not self._c:
            raise ConnectionError("MQTT cerrado")
        info = self._c.publish(self.t_in, line, qos=0)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise ConnectionError(f"MQTT: no se pudo publicar ({info.rc})")

    async def close(self) -> None:
        self._closing = True
        if self._c:
            c, self._c = self._c, None
            await asyncio.to_thread(c.disconnect)
            c.loop_stop()
