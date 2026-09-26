"""Arranque: python -m app [--host 0.0.0.0] [--port 8000]"""
import argparse

import uvicorn

from .config import settings
from .main import create_app


def main():
    ap = argparse.ArgumentParser(description="Gateway de la HMI del balancín")
    ap.add_argument("--host", default=settings.host,
                    help="127.0.0.1 = sólo este PC; 0.0.0.0 = también otros equipos de la red (celular)")
    ap.add_argument("--port", type=int, default=settings.port)
    args = ap.parse_args()
    uvicorn.run(create_app(), host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
