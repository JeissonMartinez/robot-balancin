# Gateway: API para la HMI

El gateway (`hmi/gateway`, Python + FastAPI) es el único proceso que habla con el robot. La HMI
web, y cualquier otro cliente (un notebook, un script de análisis), usa esta API. El protocolo
robot ↔ gateway está en [PROTOCOLO.md](PROTOCOLO.md).

Base: `http://<host>:8000`. Sin autenticación todavía (ver PLAN §8): por defecto sólo escucha en
`127.0.0.1`.

---

## 1. REST

| Método y ruta | Cuerpo / parámetros | Respuesta |
|---|---|---|
| `GET /api/health` | — | `{"ok": true}` |
| `GET /api/transports` | — | Puertos serie disponibles (`usb: true` primero) y velocidad |
| `GET /api/status` | — | Estado del enlace (ver §3) |
| `POST /api/connect` | `{"transport": "serial", "port": "/dev/cu...", "baud": 921600}` o `{"transport": "demo"}` | Estado; `502` con `detail` si falla |
| `POST /api/disconnect` | — | Estado. Deja la telemetría del robot en texto a 10 Hz, como el monitor serie espera |
| `GET /api/sessions?limit=50` | — | Sesiones, la más reciente primero, con `frames` (tramas guardadas) |
| `GET /api/sessions/{id}` | — | Una sesión, con `params` al conectar |
| `GET /api/sessions/{id}/telemetry` | `t_from`, `t_to` (ms del robot), `limit` | Filas de `telemetry` |
| `GET /api/sessions/{id}/events` | — | Filas de `events` con `payload` ya decodificado |

`transport: "demo"` es un robot simulado dentro del gateway (`app/transports/demo.py`): habla el
mismo protocolo, valida los parámetros con el `schema` real del firmware y reacciona a ellos. Sirve
para desarrollar la HMI y para practicar sin el robot.

---

## 2. WebSocket `/ws`

Un mensaje JSON por trama de texto.

### Gateway → HMI

| `type` | Cuándo | Contenido |
|---|---|---|
| `snapshot` | Al abrir el socket y al (re)conectar el robot | `status`, `schema`, `params` |
| `status` | Al cambiar el estado y cada 1 s | `status` (§3) |
| `tel` | Cada 100 ms mientras llegan tramas | `frames`: tramas del robot sin el campo `type` (PROTOCOLO.md §2) |
| `params` | Tras un `set`/`defaults` aceptado o la tecla `p` | `params` completos |
| `event` | Cada evento guardado | `event`: `{kind, t_ms, host_ts, payload}` (§4) |
| `ack` | Respuesta a un comando de este cliente | `id` del cliente, `ok`, `err` y los campos extra del robot |

### HMI → gateway

```json
{"type":"cmd","id":12,"cmd":"set","params":{"kd_angle":1.3}}
```

Comandos permitidos: `hello`, `get`, `schema`, `set`, `defaults`, `save`, `calib`, `deadband`,
`estop`, `arm`. `tel` no: la telemetría la configura el gateway. El `id` lo elige el cliente y vuelve
en su `ack`; el gateway usa ids propios con el robot.

- Los comandos se envían al robot de a uno, en orden de llegada, excepto `estop`, que no espera
  turno y se reintenta hasta 3 veces (el primer envío durante una prueba de zona muerta la aborta y
  se pierde).
- Varios clientes pueden estar conectados a la vez (PC y celular). Todos reciben la telemetría, los
  eventos y los cambios de parámetros; cada `ack` sólo lo recibe quien envió el comando.

---

## 3. Estado

```json
{"state":"connected","error":null,"transport":"serial","target":"/dev/cu.wchusbserial...",
 "fw":"0.3.0","proto":1,"params_src":"nvs","session_id":4,"robot_state":"ACTIVE",
 "rate":50.0,"gaps":0,"clients":2}
```

| Campo | Significado |
|---|---|
| `state` | `disconnected`, `connecting` (configurando o tras un reinicio del robot), `connected` |
| `error` | Motivo de la última desconexión no pedida o del último intento fallido |
| `rate` | Tramas por segundo recibidas en el último segundo |
| `gaps` | Tramas perdidas en la sesión (saltos de `seq`) |
| `clients` | Pantallas conectadas al WebSocket |

---

## 4. Sesiones y eventos

Una **sesión** empieza al conectar y termina al desconectar, al perder la conexión o cuando el
robot se reinicia (un `hello` que nadie pidió). Así el tiempo del robot (`t_ms`) nunca retrocede
dentro de una sesión.

| `kind` | `payload` |
|---|---|
| `connect` | `target`, `hello` |
| `disconnect` | `target` |
| `reboot` | — (se cierra la sesión y se abre otra) |
| `connection_lost` | `reason` |
| `cmd` | `cmd`, sus campos (p. ej. `params`), `ok`, `err` |
| `log` | `msg`: línea de texto libre del robot |

`t_ms` es el tiempo del robot en la última trama recibida antes del evento: con él la HMI dibuja
las marcas en las trazas y el análisis posterior alinea eventos con telemetría.

La base es SQLite (`hmi/gateway/data/balancin.db`, fuera de git); esquema en
`app/storage/db.py`. Se puede abrir con cualquier cliente SQLite o con pandas:

```python
import sqlite3, pandas as pd
con = sqlite3.connect("hmi/gateway/data/balancin.db")
df = pd.read_sql("SELECT * FROM telemetry WHERE session_id = 4", con)
```
