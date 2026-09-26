"""Arranque: python -m app [--host 0.0.0.0] [--port 8000]"""
import argparse

import uvicorn

from .config import lan_addresses, settings
from .main import create_app


def main():
    ap = argparse.ArgumentParser(description="Gateway de la HMI del balancín")
    ap.add_argument("--host", default=settings.host,
                    help="127.0.0.1 = sólo este PC; 0.0.0.0 = también otros equipos de la red (celular)")
    ap.add_argument("--port", type=int, default=settings.port)
    args = ap.parse_args()
    settings.host, settings.port = args.host, args.port

    print(f"\n  HMI en este PC:   http://127.0.0.1:{args.port}")
    if args.host in ("0.0.0.0", "::"):
        for ip in lan_addresses():
            print(f"  HMI en la red:    http://{ip}:{args.port}   (celular en la misma WiFi)")
    else:
        print("  Sólo este PC. Para el celular: python -m app --host 0.0.0.0")
    print(flush=True)
    uvicorn.run(create_app(), host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
