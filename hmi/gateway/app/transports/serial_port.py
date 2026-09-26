"""Transporte por puerto serie (USB) con pyserial.

La lectura corre en un hilo porque pyserial es bloqueante; cada línea se entrega al
event loop con call_soon_threadsafe.
"""
from __future__ import annotations

import asyncio
import threading

import serial
from serial.tools import list_ports

from .base import CloseHandler, LineHandler, Transport

DEFAULT_BAUD = 921600  # = SERIAL_BAUD en include/config.h


def available_ports() -> list[dict]:
    """Puertos serie del sistema, los adaptadores USB primero."""
    ports = []
    for p in list_ports.comports():
        usb = p.vid is not None
        ports.append({"device": p.device, "description": p.description or "", "usb": usb})
    ports.sort(key=lambda p: (not p["usb"], p["device"]))
    return ports


class SerialTransport(Transport):
    kind = "serial"

    def __init__(self, port: str, baud: int = DEFAULT_BAUD):
        self.port = port
        self.baud = baud
        self._ser: serial.Serial | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._write_lock = threading.Lock()

    @property
    def target(self) -> str:
        return self.port

    async def open(self, on_line: LineHandler, on_close: CloseHandler) -> None:
        loop = asyncio.get_running_loop()
        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = self.baud
        ser.timeout = 0.1
        # DTR y RTS inactivos: con el circuito de auto-reset del ESP32, abrir el puerto
        # no reinicia el robot ni lo deja en modo descarga.
        ser.dtr = False
        ser.rts = False
        await asyncio.to_thread(ser.open)
        self._ser = ser
        self._stop.clear()

        def reader():
            buf = bytearray()
            reason = None
            try:
                while not self._stop.is_set():
                    chunk = ser.read(ser.in_waiting or 1)
                    if not chunk:
                        continue
                    buf.extend(chunk)
                    while True:
                        i = buf.find(b"\n")
                        if i < 0:
                            break
                        line = buf[:i].decode("utf-8", "replace").rstrip("\r")
                        del buf[: i + 1]
                        loop.call_soon_threadsafe(on_line, line)
            except (serial.SerialException, OSError) as e:
                reason = f"puerto serie perdido: {e}"
            if not self._stop.is_set():
                loop.call_soon_threadsafe(on_close, reason)

        self._thread = threading.Thread(target=reader, name=f"serial-{self.port}", daemon=True)
        self._thread.start()

    async def send(self, line: str) -> None:
        if not self._ser:
            raise ConnectionError("puerto cerrado")
        data = (line + "\n").encode()

        def write():
            with self._write_lock:
                self._ser.write(data)

        await asyncio.to_thread(write)

    async def close(self) -> None:
        self._stop.set()
        if self._thread:
            await asyncio.to_thread(self._thread.join, 1.0)
        if self._ser:
            await asyncio.to_thread(self._ser.close)
        self._ser = None
        self._thread = None
