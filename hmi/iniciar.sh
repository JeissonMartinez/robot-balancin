#!/usr/bin/env bash
# Arranca el laboratorio en este equipo (macOS o Linux) con un solo comando:
#   hmi/iniciar.sh                 # HMI accesible desde otros equipos de la red
#   hmi/iniciar.sh --clave xyz     # ... con clave de acceso
#   hmi/iniciar.sh --mqtt          # ... y además el broker MQTT (Mosquitto)
#   hmi/iniciar.sh --solo-este-pc  # HMI sólo en este equipo
#   hmi/iniciar.sh --puerto 8080   # otro puerto (por defecto 8000)
# La primera vez instala lo necesario (Python ≥ 3.11 y Node ≥ 20 deben estar instalados).
# Recompila la HMI si cambió su código.
set -euo pipefail
HMI="$(cd "$(dirname "$0")" && pwd)"
HOST="0.0.0.0"
ARGS=()
MQTT=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --mqtt) MQTT=1 ;;
    --solo-este-pc) HOST="127.0.0.1" ;;
    --clave) ARGS+=(--clave "$2"); shift ;;
    --puerto) ARGS+=(--port "$2"); shift ;;
    *) echo "Opción desconocida: $1" >&2; exit 2 ;;
  esac
  shift
done

# Gateway: entorno de Python
cd "$HMI/gateway"
if [[ ! -x .venv/bin/python ]]; then
  echo "» Preparando el gateway (una sola vez)..."
  python3 -m venv .venv
fi
if [[ requirements.txt -nt .venv/.instalado || ! -f .venv/.instalado ]]; then
  .venv/bin/pip install -q --upgrade pip
  .venv/bin/pip install -q -r requirements.txt
  touch .venv/.instalado
fi

# HMI: compilar si falta o si su código es más nuevo que la compilación
cd "$HMI/web"
if [[ ! -d node_modules || package-lock.json -nt node_modules ]]; then
  echo "» Instalando dependencias de la HMI..."
  npm ci --no-audit --no-fund --silent
fi
if [[ ! -f dist/index.html ]] || [[ -n "$(find src index.html public -newer dist/index.html -print -quit)" ]]; then
  echo "» Compilando la HMI..."
  npm run build --silent
fi

# Broker MQTT opcional (si ya hay uno escuchando en 1883, se usa ese)
port_open() { python3 -c "import socket,sys; socket.create_connection(('127.0.0.1', $1), 0.5)" 2>/dev/null; }
if [[ $MQTT == 1 ]] && port_open 1883; then
  echo "» Ya hay un broker MQTT en el puerto 1883: se usa ese"
elif [[ $MQTT == 1 ]]; then
  if command -v mosquitto >/dev/null || [[ -x /opt/homebrew/sbin/mosquitto ]]; then
    BROKER="$(command -v mosquitto || echo /opt/homebrew/sbin/mosquitto)"
    "$BROKER" -c "$HMI/mosquitto/mosquitto-local.conf" -d
    sleep 0.5
    if port_open 1883; then echo "» Broker MQTT en el puerto 1883"; else echo "» Aviso: el broker no arrancó" >&2; fi
    trap 'pkill -f "mosquitto -c $HMI/mosquitto/mosquitto-local.conf" || true' EXIT
  else
    echo "» Aviso: no se encontró mosquitto (brew install mosquitto). Sigue sin broker." >&2
  fi
fi

cd "$HMI/gateway"
# Sin exec: así, al salir con Ctrl+C, el trap detiene también el broker
.venv/bin/python -m app --host "$HOST" ${ARGS[@]+"${ARGS[@]}"}
