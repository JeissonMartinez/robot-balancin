"""Configuración del gateway por variables de entorno (o argumentos de `python -m app`)."""
from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

GATEWAY_DIR = Path(__file__).resolve().parent.parent


@dataclass
class Settings:
    # 127.0.0.1: sólo este PC. 0.0.0.0: también el celular en la misma red (sin
    # autenticación todavía: usarlo sólo en redes de confianza, p. ej. el AP del robot).
    host: str = os.environ.get("BALANCIN_HOST", "127.0.0.1")
    port: int = int(os.environ.get("BALANCIN_PORT", "8000"))
    db_path: Path = Path(os.environ.get("BALANCIN_DB", GATEWAY_DIR / "data" / "balancin.db"))
    # Build de la HMI (npm run build en hmi/web). Si existe, el gateway la sirve en "/".
    web_dist: Path = Path(os.environ.get("BALANCIN_WEB_DIST", GATEWAY_DIR.parent / "web" / "dist"))
    # Clave de acceso opcional: si se define, la HMI la pide una vez por equipo. Vacía = abierto.
    clave: str = os.environ.get("BALANCIN_CLAVE", "")


settings = Settings()


def lan_addresses() -> list[str]:
    """IPv4 de este PC en la red local (para abrir la HMI desde el celular)."""
    import socket

    addrs = set()
    try:  # la interfaz con la que se sale a la red (no envía nada)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.0.2.1", 9))
            addrs.add(s.getsockname()[0])
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(info[4][0])
    except OSError:
        pass
    return sorted(a for a in addrs if not a.startswith("127."))
