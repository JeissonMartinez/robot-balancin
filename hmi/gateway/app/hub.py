"""Clientes WebSocket de la HMI conectados al gateway."""
from __future__ import annotations

import asyncio
import json

from fastapi import WebSocket


class Hub:
    def __init__(self):
        self._clients: set[WebSocket] = set()

    def add(self, ws: WebSocket):
        self._clients.add(ws)

    def remove(self, ws: WebSocket):
        self._clients.discard(ws)

    @property
    def count(self) -> int:
        return len(self._clients)

    async def broadcast(self, msg: dict):
        if not self._clients:
            return
        text = json.dumps(msg, ensure_ascii=False)
        clients = list(self._clients)
        results = await asyncio.gather(*(self._send(ws, text) for ws in clients))
        for ws, ok in zip(clients, results):
            if not ok:
                self._clients.discard(ws)

    @staticmethod
    async def _send(ws: WebSocket, text: str) -> bool:
        try:
            # Un cliente lento (celular con mala señal) no debe frenar a los demás
            await asyncio.wait_for(ws.send_text(text), timeout=1.0)
            return True
        except Exception:
            return False
