"""Enlace con el robot: único componente que habla el protocolo v1 (docs/hmi/PROTOCOLO.md).

- Abre el transporte, configura la telemetría JSON a 50 Hz y abre una sesión.
- Correlaciona cada comando con su ack por `id`. Los comandos van de a uno (el
  firmware los atiende en serie), salvo `estop`, que no espera turno.
- Guarda telemetría y eventos en SQLite por lotes y los reparte a la HMI:
  telemetría en paquetes cada 100 ms, estado cada 1 s.
- Detecta un reinicio del robot (un `hello` que nadie pidió): cierra la sesión,
  reconfigura la telemetría y abre otra.
- Si la conexión se pierde y el transporte lo permite (WiFi), reintenta cada
  RECONNECT_S segundos hasta recuperarla o hasta que se pida desconectar.
"""
from __future__ import annotations

import asyncio
import json
import logging
import time

from .hub import Hub
from .storage.db import Database
from .transports.base import Transport

log = logging.getLogger("robot")

PROTOCOL_VERSION = 1
# Comandos que la HMI puede enviar. `tel` no: la telemetría la administra el gateway.
CLIENT_COMMANDS = {"hello", "get", "schema", "set", "defaults", "save", "calib", "deadband", "estop", "arm", "wifi"}
TIMEOUTS = {"save": 6.0, "calib": 8.0, "deadband": 40.0}
DEFAULT_TIMEOUT = 2.0
TICK = 0.1  # s, período del lazo de reparto
RECONNECT_S = 2.0


class RobotLink:
    def __init__(self, db: Database, hub: Hub):
        self.db = db
        self.hub = hub
        self.transport: Transport | None = None
        self.state = "disconnected"  # disconnected | connecting | connected
        self.error: str | None = None
        self.hello: dict | None = None
        self.schema: list | None = None
        self.params: dict | None = None
        self.session_id: int | None = None
        self.robot_state: str | None = None
        self.last_t: int | None = None
        self.last_seq: int | None = None
        self.gaps = 0
        self.rate = 0.0

        self._pending: dict[int, tuple[str, asyncio.Future]] = {}
        self._next_id = 1
        self._lock = asyncio.Lock()
        self._tel_db: list[tuple[float, dict]] = []
        self._tel_ws: list[dict] = []
        self._events: list[tuple] = []
        self._frames = 0
        self._pump: asyncio.Task | None = None
        self._reconfiguring = False
        self._factory = None  # crea un transporte igual para reconectar (None = no reconectar)
        self._retry: asyncio.Task | None = None

    # ================================================================ estado
    def status(self) -> dict:
        return {
            "state": self.state,
            "error": self.error,
            "transport": self.transport.kind if self.transport else None,
            "target": self.transport.target if self.transport else None,
            "fw": self.hello.get("fw") if self.hello else None,
            "proto": self.hello.get("proto") if self.hello else None,
            "params_src": self.hello.get("params_src") if self.hello else None,
            "session_id": self.session_id,
            "robot_state": self.robot_state,
            "rate": round(self.rate, 1),
            "gaps": self.gaps,
            "clients": self.hub.count,
            "retrying": self._retry is not None,
        }

    def snapshot(self) -> dict:
        return {"type": "snapshot", "status": self.status(), "schema": self.schema, "params": self.params}

    async def _broadcast_status(self):
        await self.hub.broadcast({"type": "status", "status": self.status()})

    # ================================================================ conexión
    async def connect(self, transport: Transport, reconnect=None):
        """`reconnect`: función que crea un transporte equivalente; si se da, una conexión
        perdida se reintenta sola (WiFi)."""
        self._stop_retry()
        if self.state != "disconnected":
            await self.disconnect()
        self._factory = reconnect
        await self._open(transport)

    async def _open(self, transport: Transport):
        self.state, self.error = "connecting", None
        await self._broadcast_status()
        try:
            await transport.open(self._on_line, self._on_transport_closed)
            self.transport = transport
            async with self._lock:
                await self._configure()
        except Exception as e:
            log.warning("no se pudo conectar: %s", e)
            await self._teardown()
            self.error = str(e) or type(e).__name__
            await self._broadcast_status()
            raise
        self._pump = asyncio.create_task(self._pump_loop())
        await self._broadcast_status()
        await self.hub.broadcast(self.snapshot())

    def _stop_retry(self):
        if self._retry and not self._retry.done():
            self._retry.cancel()
        self._retry = None

    async def disconnect(self):
        self._factory = None
        self._stop_retry()
        if not self.transport:
            return  # ya desconectado: nada que registrar
        if self.state == "connected" and self.transport.kind == "serial":
            # Deja el monitor serie como estaba: texto a 10 Hz
            try:
                await self._command_raw("tel", {"fmt": "text", "div": 5, "on": True}, timeout=0.5)
            except Exception:
                pass
        self._event("disconnect", {"target": self.transport.target})
        await self._teardown()
        await self._broadcast_status()

    async def _teardown(self):
        if self._pump:
            self._pump.cancel()
            try:
                await self._pump
            except asyncio.CancelledError:
                pass
            self._pump = None
        await self._flush_db()
        if self.session_id:
            await asyncio.to_thread(self.db.end_session, self.session_id)
        if self.transport:
            try:
                await self.transport.close()
            except Exception:
                pass
        for _, fut in self._pending.values():
            if not fut.done():
                fut.set_exception(ConnectionError("conexión cerrada"))
        self._pending.clear()
        self.transport = None
        self.state = "disconnected"
        self.session_id = None
        self.robot_state = None
        self.last_seq = self.last_t = None
        self.rate = 0.0

    def _on_transport_closed(self, reason: str | None):
        asyncio.create_task(self._lost(reason or "conexión perdida"))

    async def _lost(self, reason: str):
        log.warning(reason)
        self._event("connection_lost", {"reason": reason})
        await self._teardown()
        self.error = reason
        await self._broadcast_status()
        if self._factory and not self._retry:
            self._retry = asyncio.create_task(self._retry_loop(reason))

    async def _retry_loop(self, reason: str):
        attempt = 0
        try:
            while self._factory:
                attempt += 1
                self.error = f"{reason} · reintentando ({attempt})…"
                await self._broadcast_status()
                await asyncio.sleep(RECONNECT_S)
                if not self._factory:
                    return
                try:
                    await self._open(self._factory())
                    self._event("reconnect", {"attempts": attempt})
                    return
                except Exception:
                    continue
        finally:
            self._retry = None

    async def _configure(self):
        """Silencia la telemetría, lee versión, schema y parámetros, abre sesión y activa
        la telemetría JSON a 50 Hz. Reintenta el primer comando por si el robot arranca."""
        self.hello = None
        for attempt in range(3):
            try:
                await self._command_raw("tel", {"on": False}, timeout=1.5)
                break
            except asyncio.TimeoutError:
                if attempt == 2:
                    raise TimeoutError("el robot no responde (¿firmware ≥ 0.2.0? ¿velocidad del puerto?)")
        await self._command_raw("hello")
        if not self.hello:
            raise RuntimeError("el robot no envió hello")
        if self.hello.get("proto") != PROTOCOL_VERSION:
            raise RuntimeError(f"protocolo {self.hello.get('proto')} no soportado (se espera {PROTOCOL_VERSION})")
        self.schema = (await self._command_raw("schema"))["schema"]
        self.params = (await self._command_raw("get"))["params"]

        if self.session_id:
            await asyncio.to_thread(self.db.end_session, self.session_id)
        self.session_id = await asyncio.to_thread(
            self.db.create_session, self.transport.kind, self.transport.target,
            self.hello.get("fw"), self.hello.get("proto"), self.params)
        self.last_seq = None
        self.gaps = 0
        await self._command_raw("tel", {"on": True, "fmt": "json", "div": 1})
        self.state = "connected"
        self._event("connect", {"target": self.transport.target, "hello": self.hello})

    async def _handle_reboot(self):
        if self._reconfiguring:
            return
        self._reconfiguring = True
        try:
            self._event("reboot", None)
            self.state = "connecting"
            await self._broadcast_status()
            await asyncio.sleep(0.3)  # el robot termina de imprimir su arranque
            async with self._lock:
                await self._configure()
            await self._broadcast_status()
            await self.hub.broadcast(self.snapshot())
        except Exception as e:
            await self._lost(f"no se pudo reconfigurar tras el reinicio: {e}")
        finally:
            self._reconfiguring = False
        self._factory = None  # crea un transporte igual para reconectar (None = no reconectar)
        self._retry: asyncio.Task | None = None

    # ================================================================ comandos
    async def _command_raw(self, cmd: str, fields: dict | None = None, timeout: float | None = None) -> dict:
        if not self.transport:
            raise ConnectionError("sin conexión")
        mid = self._next_id
        self._next_id += 1
        fut = asyncio.get_running_loop().create_future()
        self._pending[mid] = (cmd, fut)
        try:
            await self.transport.send(json.dumps({"type": "cmd", "id": mid, "cmd": cmd, **(fields or {})}))
            return await asyncio.wait_for(fut, timeout or TIMEOUTS.get(cmd, DEFAULT_TIMEOUT))
        finally:
            self._pending.pop(mid, None)

    async def command(self, cmd: str, fields: dict | None = None, source: str | None = None) -> dict:
        """Comando pedido por la HMI. Devuelve el ack del robot (sin su id).
        `source` queda en el evento (p. ej. el juego de parámetros que se cargó)."""
        if cmd not in CLIENT_COMMANDS:
            return {"ok": False, "err": f"comando no permitido: {cmd}"}
        if self.state != "connected":
            return {"ok": False, "err": "robot no conectado"}
        fields = {k: v for k, v in (fields or {}).items() if k not in ("type", "id", "cmd")}
        try:
            if cmd == "estop":
                ack = await self._estop()
            else:
                async with self._lock:
                    ack = await self._command_raw(cmd, fields)
        except asyncio.TimeoutError:
            ack = {"ok": False, "err": f"sin respuesta del robot a {cmd}"}
        except ConnectionError as e:
            ack = {"ok": False, "err": str(e)}
        ack = {k: v for k, v in ack.items() if k not in ("type", "id")}

        payload = {"cmd": cmd, **fields, "ok": ack.get("ok"), "err": ack.get("err")}
        if source:
            payload["source"] = source
        self._event("cmd", payload)
        if ack.get("ok") and "params" in ack:
            self.params = ack["params"]
            await self.hub.broadcast({"type": "params", "params": self.params})
        return ack

    async def _estop(self) -> dict:
        """Sin esperar turno. Si hay una prueba de zona muerta en curso, el primer envío
        la aborta y se pierde; por eso se reintenta."""
        for _ in range(3):
            try:
                return await self._command_raw("estop", timeout=0.7)
            except asyncio.TimeoutError:
                continue
        raise asyncio.TimeoutError()

    # ================================================================ entrada
    def _on_line(self, line: str):
        if line.startswith("{"):
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                return self._robot_log(line)
            kind = msg.get("type")
            if kind == "tel":
                return self._on_tel(msg)
            if kind == "ack":
                entry = self._pending.get(msg.get("id"))
                if entry and not entry[1].done():
                    entry[1].set_result(msg)
                return
            if kind == "hello":
                solicited = any(c == "hello" for c, _ in self._pending.values())
                self.hello = msg
                if not solicited and self.state == "connected":
                    asyncio.create_task(self._handle_reboot())
                return
            if kind == "params":
                self.params = msg.get("params")
                asyncio.create_task(self.hub.broadcast({"type": "params", "params": self.params}))
            return
        self._robot_log(line)

    def _robot_log(self, line: str):
        line = line.strip()
        # Telemetría en texto (antes de activar JSON): no es un log
        if not line or line.startswith("Ang:"):
            return
        self._event("log", {"msg": line})

    def _on_tel(self, msg: dict):
        now = time.time()
        seq = msg.get("seq")
        if self.last_seq is not None and seq is not None and seq > self.last_seq + 1:
            self.gaps += seq - self.last_seq - 1
        self.last_seq = seq
        self.last_t = msg.get("t")
        self.robot_state = msg.get("st")
        self._frames += 1
        if self.session_id and self.state == "connected":
            self._tel_db.append((now, msg))
        frame = dict(msg)
        frame.pop("type", None)
        self._tel_ws.append(frame)

    def record(self, kind: str, payload):
        """Evento originado fuera del enlace (p. ej. se guardó un juego de parámetros)."""
        self._event(kind, payload)

    def _event(self, kind: str, payload):
        ev = {"kind": kind, "t_ms": self.last_t, "host_ts": time.time(), "payload": payload}
        self._events.append((self.session_id, ev))
        try:
            asyncio.get_running_loop().create_task(self.hub.broadcast({"type": "event", "event": ev}))
        except RuntimeError:
            pass

    # ================================================================ salida
    async def _flush_db(self):
        tel, self._tel_db = self._tel_db, []
        events, self._events = self._events, []
        session = self.session_id

        def write():
            if session and tel:
                self.db.insert_telemetry(session, tel)
            for sid, ev in events:
                self.db.add_event(sid, ev["kind"], ev["payload"], ev["t_ms"], ev["host_ts"])

        if tel or events:
            await asyncio.to_thread(write)

    async def _pump_loop(self):
        tick = 0
        last_rate_t = time.monotonic()
        while True:
            await asyncio.sleep(TICK)
            tick += 1
            if self._tel_ws:
                frames, self._tel_ws = self._tel_ws, []
                await self.hub.broadcast({"type": "tel", "frames": frames})
            if tick % 5 == 0:
                try:
                    await self._flush_db()
                except Exception as e:  # la HMI sigue funcionando aunque falle el disco
                    log.error("error al guardar en la base: %s", e)
            if tick % 10 == 0:
                now = time.monotonic()
                self.rate = self._frames / (now - last_rate_t)
                self._frames, last_rate_t = 0, now
                await self._broadcast_status()
