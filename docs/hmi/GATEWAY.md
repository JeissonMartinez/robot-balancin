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

---

## 5. Revisar la base de datos

Archivo: `hmi/gateway/data/balancin.db` (SQLite, fuera de git; esquema en `app/storage/db.py`).
Junto a él aparecen `balancin.db-wal` y `balancin.db-shm`: son parte de la base mientras el gateway
está abierto (modo WAL). No borrarlos ni copiar el `.db` solo con el gateway corriendo.

| Tabla | Una fila por | Columnas principales |
|---|---|---|
| `sessions` | conexión (o reinicio del robot) | `id`, `started_at`, `ended_at` (UTC), `transport`, `target`, `fw`, `params_json` |
| `telemetry` | trama (50 por segundo) | `session_id`, `host_ts` (hora del PC, epoch s), `seq`, `t_ms`, `ang`, `ref`, `w`, `pwm`, `pwm_m`, `rpm_l`, `rpm_r`, `kp`, `dt`, `u_p`, `u_i`, `u_d`, `st` |
| `events` | comando, log del robot o cambio de conexión | `session_id`, `host_ts`, `t_ms`, `kind`, `payload` (JSON) |
| `param_sets` | juego de parámetros guardado (F2) | `name`, `params_json` |

Leer es seguro con el gateway corriendo. **Modificar o borrar, sólo con el gateway detenido.**

### a) Desde el navegador (sin instalar nada)

Con el gateway corriendo:

- `http://127.0.0.1:8000/docs`: documentación interactiva de la API; cada ruta tiene *Try it out*.
- `http://127.0.0.1:8000/api/sessions`: lista de sesiones con el número de tramas.
- `http://127.0.0.1:8000/api/sessions/5/events`
- `http://127.0.0.1:8000/api/sessions/5/telemetry?limit=20`

### b) Terminal (`sqlite3`, incluido en macOS)

```bash
cd hmi/gateway
sqlite3 -readonly -header -column data/balancin.db
```

```sql
.tables
-- sesiones con su duración y tramas
SELECT id, started_at, ended_at, transport, fw,
       (SELECT COUNT(*) FROM telemetry t WHERE t.session_id = s.id) AS tramas
FROM sessions s;

-- qué se hizo en una sesión
SELECT datetime(host_ts, 'unixepoch', 'localtime') AS hora, t_ms, kind, payload
FROM events WHERE session_id = 5;

-- resumen del ángulo mientras controlaba
SELECT session_id, MIN(ang), MAX(ang), AVG(ABS(ang)) FROM telemetry
WHERE st = 'ACTIVE' GROUP BY session_id;
.quit
```

Exportar una sesión a CSV (para Excel, MATLAB, ...):

```bash
sqlite3 -readonly -header -csv data/balancin.db \
  "SELECT * FROM telemetry WHERE session_id = 5 ORDER BY t_ms" > sesion5.csv
```

### c) Con interfaz gráfica

[DB Browser for SQLite](https://sqlitebrowser.org) (`brew install --cask db-browser-for-sqlite`):
abrir `balancin.db` con **Open Database Read Only** para no bloquear al gateway. En VS Code sirve la
extensión *SQLite Viewer*.

### d) Python / Jupyter

```python
import sqlite3, pandas as pd
con = sqlite3.connect("file:hmi/gateway/data/balancin.db?mode=ro", uri=True)
tel = pd.read_sql("SELECT * FROM telemetry WHERE session_id = 5 ORDER BY t_ms", con)
tel["t"] = (tel.t_ms - tel.t_ms.iloc[0]) / 1000      # s desde el inicio de la sesión
tel.plot(x="t", y=["ang", "ref"])
```

### Borrar sesiones

Con el gateway **detenido** (la clave foránea borra también su telemetría y eventos):

```bash
sqlite3 data/balancin.db "PRAGMA foreign_keys=ON; DELETE FROM sessions WHERE id IN (1,2,3); VACUUM;"
```

En F3 la HMI tendrá la tabla de historial con filtros, exportación y borrado.
