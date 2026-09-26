"""Base de datos local (SQLite) del laboratorio.

Tablas (docs/hmi/PLAN.md §5):
- sessions:   una por conexión al robot (o por reinicio del robot).
- telemetry:  una fila por trama, columnas con los campos del protocolo.
- events:     comandos, respuestas, cambios de parámetros y logs del robot.
- param_sets: juegos de parámetros con nombre (se usa desde F2).

Todas las funciones son síncronas y seguras entre hilos (un lock); el gateway las
llama con asyncio.to_thread para no frenar el event loop.
"""
from __future__ import annotations

import json
import sqlite3
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = 1

# Campo del protocolo → columna. El orden es el de las inserciones.
TEL_FIELDS = [
    ("seq", "seq"), ("t", "t_ms"), ("ang", "ang"), ("ref", "ref"), ("w", "w"),
    ("pwm", "pwm"), ("pwmM", "pwm_m"), ("rpmL", "rpm_l"), ("rpmR", "rpm_r"),
    ("kp", "kp"), ("dt", "dt"), ("uP", "u_p"), ("uI", "u_i"), ("uD", "u_d"), ("st", "st"),
]

DDL = """
CREATE TABLE IF NOT EXISTS sessions (
    id          INTEGER PRIMARY KEY,
    started_at  TEXT NOT NULL,
    ended_at    TEXT,
    transport   TEXT NOT NULL,
    target      TEXT,
    fw          TEXT,
    proto       INTEGER,
    params_json TEXT,
    notes       TEXT
);
CREATE TABLE IF NOT EXISTS telemetry (
    session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    host_ts REAL NOT NULL,
    seq INTEGER, t_ms INTEGER,
    ang REAL, ref REAL, w REAL, pwm REAL, pwm_m REAL, rpm_l REAL, rpm_r REAL,
    kp REAL, dt REAL, u_p REAL, u_i REAL, u_d REAL, st TEXT
);
CREATE INDEX IF NOT EXISTS telemetry_session_t ON telemetry(session_id, t_ms);
CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY,
    session_id INTEGER REFERENCES sessions(id) ON DELETE CASCADE,
    host_ts    REAL NOT NULL,
    t_ms       INTEGER,
    kind       TEXT NOT NULL,
    payload    TEXT
);
CREATE INDEX IF NOT EXISTS events_session ON events(session_id, host_ts);
CREATE TABLE IF NOT EXISTS param_sets (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    created_at  TEXT NOT NULL,
    notes       TEXT,
    fw          TEXT,
    params_json TEXT NOT NULL
);
"""


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class Database:
    def __init__(self, path: Path | str):
        self.path = Path(path)
        if str(path) != ":memory:":
            self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._con = sqlite3.connect(str(path), check_same_thread=False)
        self._con.row_factory = sqlite3.Row
        with self._lock:
            self._con.execute("PRAGMA journal_mode=WAL")
            self._con.execute("PRAGMA foreign_keys=ON")
            version = self._con.execute("PRAGMA user_version").fetchone()[0]
            if version > SCHEMA_VERSION:
                raise RuntimeError(f"la base {path} es de una versión más nueva ({version})")
            self._con.executescript(DDL)
            self._con.execute(f"PRAGMA user_version={SCHEMA_VERSION}")
            self._con.commit()

    def close(self):
        with self._lock:
            self._con.close()

    # ---------------------------------------------------------------- sesiones
    def create_session(self, transport: str, target: str, fw: str | None, proto: int | None,
                       params: dict | None) -> int:
        with self._lock:
            cur = self._con.execute(
                "INSERT INTO sessions(started_at, transport, target, fw, proto, params_json) VALUES (?,?,?,?,?,?)",
                (now_iso(), transport, target, fw, proto, json.dumps(params) if params else None),
            )
            self._con.commit()
            return cur.lastrowid

    def end_session(self, session_id: int):
        with self._lock:
            self._con.execute("UPDATE sessions SET ended_at=? WHERE id=? AND ended_at IS NULL",
                              (now_iso(), session_id))
            self._con.commit()

    def list_sessions(self, limit: int = 50) -> list[dict]:
        with self._lock:
            rows = self._con.execute(
                """SELECT s.*, (SELECT COUNT(*) FROM telemetry t WHERE t.session_id = s.id) AS frames
                   FROM sessions s ORDER BY s.id DESC LIMIT ?""", (limit,)).fetchall()
        return [_session_dict(r) for r in rows]

    def get_session(self, session_id: int) -> dict | None:
        with self._lock:
            r = self._con.execute(
                """SELECT s.*, (SELECT COUNT(*) FROM telemetry t WHERE t.session_id = s.id) AS frames
                   FROM sessions s WHERE s.id=?""", (session_id,)).fetchone()
        return _session_dict(r) if r else None

    # ---------------------------------------------------------------- telemetría
    def insert_telemetry(self, session_id: int, frames: list[tuple[float, dict]]):
        """`frames`: lista de (host_ts, trama JSON). Los campos ausentes quedan NULL."""
        if not frames:
            return
        cols = ", ".join(c for _, c in TEL_FIELDS)
        marks = ", ".join("?" * (len(TEL_FIELDS) + 2))
        rows = [(session_id, ts, *(f.get(k) for k, _ in TEL_FIELDS)) for ts, f in frames]
        with self._lock:
            self._con.executemany(f"INSERT INTO telemetry(session_id, host_ts, {cols}) VALUES ({marks})", rows)
            self._con.commit()

    def get_telemetry(self, session_id: int, t_from: int | None = None, t_to: int | None = None,
                      limit: int = 100_000) -> list[dict]:
        q = "SELECT * FROM telemetry WHERE session_id=?"
        args: list = [session_id]
        if t_from is not None:
            q += " AND t_ms >= ?"
            args.append(t_from)
        if t_to is not None:
            q += " AND t_ms <= ?"
            args.append(t_to)
        q += " ORDER BY rowid LIMIT ?"
        args.append(limit)
        with self._lock:
            return [dict(r) for r in self._con.execute(q, args).fetchall()]

    # ---------------------------------------------------------------- eventos
    def add_event(self, session_id: int | None, kind: str, payload=None, t_ms: int | None = None,
                  host_ts: float | None = None) -> int:
        with self._lock:
            cur = self._con.execute(
                "INSERT INTO events(session_id, host_ts, t_ms, kind, payload) VALUES (?,?,?,?,?)",
                (session_id, host_ts or time.time(), t_ms, kind,
                 None if payload is None else json.dumps(payload, ensure_ascii=False)),
            )
            self._con.commit()
            return cur.lastrowid

    def get_events(self, session_id: int, limit: int = 10_000) -> list[dict]:
        with self._lock:
            rows = self._con.execute(
                "SELECT * FROM events WHERE session_id=? ORDER BY id LIMIT ?", (session_id, limit)).fetchall()
        out = []
        for r in rows:
            d = dict(r)
            d["payload"] = json.loads(d["payload"]) if d["payload"] else None
            out.append(d)
        return out


def _session_dict(r: sqlite3.Row) -> dict:
    d = dict(r)
    d["params"] = json.loads(d.pop("params_json")) if d.get("params_json") else None
    return d
