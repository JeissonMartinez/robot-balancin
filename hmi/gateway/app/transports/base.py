"""Interfaz común de los transportes robot ↔ gateway.

Un transporte solo mueve líneas de texto: no interpreta el protocolo. RobotLink
(app/robot.py) es quien entiende los mensajes. Así Serial, WebSocket (F4) y MQTT (F5)
son intercambiables.
"""
from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Callable

LineHandler = Callable[[str], None]
CloseHandler = Callable[[str | None], None]


class Transport(ABC):
    #: Nombre corto para la HMI y la base de datos ("serial", "demo", ...).
    kind: str = ""

    @abstractmethod
    async def open(self, on_line: LineHandler, on_close: CloseHandler) -> None:
        """Abre la conexión. `on_line` recibe cada línea sin el salto final; `on_close`
        se llama una vez si la conexión se pierde sin que la cierre el gateway (con el
        motivo). Ambos se invocan en el event loop."""

    @abstractmethod
    async def send(self, line: str) -> None:
        """Envía una línea (el transporte agrega el terminador)."""

    @abstractmethod
    async def close(self) -> None:
        """Cierra la conexión. No llama a `on_close`."""

    @property
    @abstractmethod
    def target(self) -> str:
        """Destino legible: puerto, URL, ..."""
