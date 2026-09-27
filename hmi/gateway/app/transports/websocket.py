"""Transporte WiFi: WebSocket al servidor del robot (ws://<ip>/ws).

Cada mensaje del robot es una línea (JSON o texto de log), igual que por Serial, así
que RobotLink no distingue el transporte. El ping (cada 5 s, 10 s de margen) detecta si
el robot se apagó o salió de alcance; sin él, TCP puede tardar minutos en darse cuenta.
El margen es amplio porque la radio del ESP32 puede demorarse unos segundos cuando
además reenvía tráfico entre otros equipos de su red.
"""
from __future__ import annotations

import asyncio

import websockets

from .base import CloseHandler, LineHandler, Transport

DEFAULT_HOST = "192.168.4.1"  # IP del robot en su propia red (modo AP)


def ws_url(host: str) -> str:
    host = host.strip()
    if host.startswith(("ws://", "wss://")):
        return host
    return f"ws://{host.rstrip('/')}/ws"


class WebSocketTransport(Transport):
    kind = "wifi"

    def __init__(self, host: str = DEFAULT_HOST):
        self.url = ws_url(host)
        self._ws = None
        self._reader: asyncio.Task | None = None
        self._closing = False

    @property
    def target(self) -> str:
        return self.url

    async def open(self, on_line: LineHandler, on_close: CloseHandler) -> None:
        self._closing = False
        try:
            self._ws = await websockets.connect(
                self.url, open_timeout=5, ping_interval=5, ping_timeout=10, max_queue=256,
            )
        except (OSError, asyncio.TimeoutError, websockets.WebSocketException) as e:
            raise ConnectionError(f"no se pudo abrir {self.url}: {e}") from None

        async def reader():
            reason = None
            try:
                async for msg in self._ws:
                    text = msg.decode("utf-8", "replace") if isinstance(msg, bytes) else msg
                    for line in text.splitlines() or [""]:
                        on_line(line)
            except websockets.ConnectionClosed as e:
                reason = f"WiFi: conexión cerrada ({e.code})"
            except Exception as e:  # noqa: BLE001 - cualquier fallo de red cierra el enlace
                reason = f"WiFi: {e}"
            if not self._closing:
                on_close(reason or "WiFi: el robot cerró la conexión")

        self._reader = asyncio.create_task(reader())

    async def send(self, line: str) -> None:
        if not self._ws:
            raise ConnectionError("WebSocket cerrado")
        try:
            await self._ws.send(line)
        except websockets.ConnectionClosed as e:
            raise ConnectionError("WiFi: conexión cerrada") from None

    async def close(self) -> None:
        self._closing = True
        if self._ws:
            try:
                await asyncio.wait_for(self._ws.close(), 2)
            except Exception:
                pass
        if self._reader:
            self._reader.cancel()
            try:
                await self._reader
            except (asyncio.CancelledError, Exception):
                pass
        self._ws = None
        self._reader = None
