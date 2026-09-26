"""API del gateway.

REST (/api/...): conexión, puertos y consulta de sesiones guardadas.
WebSocket (/ws): tiempo real con la HMI. Mensajes en docs/hmi/GATEWAY.md.
"""
from __future__ import annotations

import asyncio
import json
import logging
from contextlib import asynccontextmanager
from typing import Literal

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .config import lan_addresses, settings
from .hub import Hub
from .robot import RobotLink
from .storage.db import Database
from .transports.demo import DemoTransport
from .transports.serial_port import DEFAULT_BAUD, SerialTransport, available_ports

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")


class ConnectRequest(BaseModel):
    transport: Literal["serial", "demo"]
    port: str | None = None
    baud: int = DEFAULT_BAUD


class ParamSetRequest(BaseModel):
    name: str
    notes: str | None = None
    # Si falta, se guardan los parámetros activos del robot conectado
    params: dict[str, float | int | bool | str] | None = None
    overwrite: bool = False


def create_app(db_path=None, web_dist=None) -> FastAPI:
    db = Database(db_path or settings.db_path)
    hub = Hub()
    link = RobotLink(db, hub)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        yield
        await link.disconnect()
        db.close()

    app = FastAPI(title="Gateway balancín", version="0.1.0", lifespan=lifespan)
    app.state.link = link
    app.state.db = db

    @app.get("/api/health")
    async def health():
        return {"ok": True}

    @app.get("/api/info")
    async def info():
        """Direcciones para abrir la HMI desde otro equipo (vacío si sólo escucha en este PC)."""
        public = settings.host in ("0.0.0.0", "::")
        urls = [f"http://{ip}:{settings.port}" for ip in await asyncio.to_thread(lan_addresses)] if public else []
        return {"host": settings.host, "port": settings.port, "lan_urls": urls}

    @app.get("/api/transports")
    async def transports():
        ports = await asyncio.to_thread(available_ports)
        return {"serial": {"ports": ports, "baud": DEFAULT_BAUD}, "demo": {}}

    @app.get("/api/status")
    async def status():
        return link.status()

    @app.post("/api/connect")
    async def connect(req: ConnectRequest):
        if req.transport == "serial":
            if not req.port:
                raise HTTPException(400, "falta el puerto")
            transport = SerialTransport(req.port, req.baud)
        else:
            transport = DemoTransport()
        try:
            await link.connect(transport)
        except Exception as e:
            raise HTTPException(502, str(e) or type(e).__name__)
        return link.status()

    @app.post("/api/disconnect")
    async def disconnect():
        await link.disconnect()
        return link.status()

    @app.get("/api/sessions")
    async def sessions(limit: int = 50):
        return await asyncio.to_thread(db.list_sessions, limit)

    @app.get("/api/sessions/{session_id}")
    async def session(session_id: int):
        s = await asyncio.to_thread(db.get_session, session_id)
        if not s:
            raise HTTPException(404, "sesión no encontrada")
        return s

    @app.get("/api/sessions/{session_id}/telemetry")
    async def session_telemetry(session_id: int, t_from: int | None = None, t_to: int | None = None,
                                limit: int = 100_000):
        return await asyncio.to_thread(db.get_telemetry, session_id, t_from, t_to, limit)

    @app.get("/api/sessions/{session_id}/events")
    async def session_events(session_id: int):
        return await asyncio.to_thread(db.get_events, session_id)

    # ------------------------------------------------------------ juegos de parámetros
    @app.get("/api/param-sets")
    async def param_sets():
        return await asyncio.to_thread(db.list_param_sets)

    @app.get("/api/param-sets/{set_id}")
    async def param_set(set_id: int):
        s = await asyncio.to_thread(db.get_param_set, set_id)
        if not s:
            raise HTTPException(404, "juego no encontrado")
        return s

    @app.post("/api/param-sets")
    async def save_param_set(req: ParamSetRequest):
        name = req.name.strip()
        if not name:
            raise HTTPException(400, "falta el nombre")
        params = req.params if req.params is not None else link.params
        if not params:
            raise HTTPException(400, "no hay parámetros: conectar el robot o enviarlos")
        fw = link.hello.get("fw") if link.hello and req.params is None else None
        try:
            s = await asyncio.to_thread(db.save_param_set, name, params, req.notes, fw, req.overwrite)
        except ValueError as e:
            raise HTTPException(409, str(e))
        link.record("param_set_saved", {"name": name, "id": s["id"]})
        await hub.broadcast({"type": "param_sets_changed"})
        return s

    @app.delete("/api/param-sets/{set_id}")
    async def delete_param_set(set_id: int):
        if not await asyncio.to_thread(db.delete_param_set, set_id):
            raise HTTPException(404, "juego no encontrado")
        await hub.broadcast({"type": "param_sets_changed"})
        return {"ok": True}

    @app.post("/api/param-sets/{set_id}/apply")
    async def apply_param_set(set_id: int):
        """Envía el juego al robot con un solo `set` (todo o nada). Las claves que el
        firmware actual no conoce se omiten y se informan."""
        s = await asyncio.to_thread(db.get_param_set, set_id)
        if not s:
            raise HTTPException(404, "juego no encontrado")
        known = {d["key"] for d in (link.schema or [])}
        params = {k: v for k, v in s["params"].items() if k in known}
        ignored = sorted(set(s["params"]) - known)
        ack = await link.command("set", {"params": params}, source=f"juego «{s['name']}»")
        return {**ack, "ignored": ignored}

    @app.websocket("/ws")
    async def ws_endpoint(ws: WebSocket):
        await ws.accept()
        hub.add(ws)
        await ws.send_text(json.dumps(link.snapshot(), ensure_ascii=False))
        tasks: set[asyncio.Task] = set()

        async def run_command(msg: dict):
            ack = await link.command(msg.get("cmd", ""), msg)
            try:
                await ws.send_text(json.dumps({"type": "ack", "id": msg.get("id"), **ack}, ensure_ascii=False))
            except Exception:
                pass

        try:
            while True:
                try:
                    msg = json.loads(await ws.receive_text())
                except json.JSONDecodeError:
                    continue
                if msg.get("type") == "cmd":
                    # En paralelo: un comando largo (deadband) no bloquea el estop
                    t = asyncio.create_task(run_command(msg))
                    tasks.add(t)
                    t.add_done_callback(tasks.discard)
        except WebSocketDisconnect:
            pass
        finally:
            hub.remove(ws)

    dist = web_dist or settings.web_dist
    if dist.is_dir():
        app.mount("/", StaticFiles(directory=dist, html=True), name="web")

    return app
