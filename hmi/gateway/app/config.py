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


settings = Settings()
