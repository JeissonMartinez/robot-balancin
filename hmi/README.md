# HMI del balancín

Laboratorio web para monitorear, sintonizar y registrar el robot. Plan y fases en
[docs/hmi/PLAN.md](../docs/hmi/PLAN.md).

```
robot ──Serial 921600──► gateway (Python, FastAPI) ──WebSocket──► HMI web (PC / celular)
                              └─ SQLite: sesiones, telemetría, eventos
```

| Carpeta | Qué es | Documentación |
|---|---|---|
| `gateway/` | Enlace con el robot, API y base de datos | [GATEWAY.md](../docs/hmi/GATEWAY.md) |
| `web/` | Interfaz (TypeScript + Vite, sin framework) | este archivo |
| `tools/` | `protocol_check.py`: verificación del protocolo con el robot | [PROTOCOLO.md](../docs/hmi/PROTOCOLO.md) |

## Requisitos

- Python ≥ 3.11 y Node ≥ 20.
- Firmware ≥ 0.2.0 en el robot (los términos P/I/D de las trazas necesitan ≥ 0.3.0).

## Instalar (una vez)

```bash
cd hmi/gateway
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt

cd ../web
npm install
npm run build
```

## Usar

```bash
cd hmi/gateway
.venv/bin/python -m app
```

Abrir <http://127.0.0.1:8000>, elegir **USB (serie)** y el puerto del robot (el adaptador USB sale
primero) y **Conectar**. Sin robot, **Robot simulado** permite probar todo.

- **Desde el celular**: arrancar con `.venv/bin/python -m app --host 0.0.0.0` y abrir
  `http://<IP del PC>:8000` en la misma red. No hay autenticación: hacerlo sólo en una red de
  confianza.
- **Parada de emergencia**: botón rojo o tecla `Esc` (en el PC).
- El monitor serie de PlatformIO y el gateway no pueden usar el puerto a la vez.
- Los datos quedan en `hmi/gateway/data/balancin.db` (fuera de git).

## Desarrollar

```bash
# terminal 1: gateway
cd hmi/gateway && .venv/bin/python -m app

# terminal 2: HMI con recarga en caliente (reenvía /api y /ws al gateway)
cd hmi/web && npm run dev
```

Pruebas del gateway (usan el robot simulado, no necesitan hardware):

```bash
cd hmi/gateway && .venv/bin/python -m pytest
```

### Estructura de `web/src`

| Carpeta | Contenido |
|---|---|
| `core/` | `protocol.ts` (tipos de los mensajes), `gateway.ts` (WebSocket con reconexión, comandos, REST) |
| `ui/` | Tema, conexión, cifras de cabecera, registro de eventos |
| `plots/` | Trazas con uPlot |
| `styles/` | `tokens.css` (colores del Simulador_Balancin + paleta de series), `base.css`, `layout.css` |

### Estructura de `gateway/app`

| Módulo | Contenido |
|---|---|
| `main.py` | Rutas REST y WebSocket; sirve `web/dist` si existe |
| `robot.py` | `RobotLink`: configuración del robot, comandos con ack, sesiones, reparto de telemetría |
| `hub.py` | Clientes WebSocket conectados |
| `transports/` | `serial_port.py`, `demo.py` (robot simulado); en F4 WebSocket, en F5 MQTT |
| `storage/db.py` | SQLite: sesiones, telemetría, eventos, juegos de parámetros |

### Colores de las trazas

Las series usan una paleta categórica en orden fijo (`--series-1..5` en `tokens.css`), validada para
daltonismo en tema claro y oscuro. Los colores institucionales (`--azul`, `--rojo`, ...) quedan para
la interfaz: son demasiado oscuros para líneas de datos. Cada gráfico tiene una sola magnitud y un
solo eje y.
