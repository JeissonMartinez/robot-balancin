# Protocolo robot ↔ HMI (v1)

Mensajes entre el firmware del balancín y el gateway de la HMI. El protocolo es el mismo sobre
cualquier transporte; hoy se implementa por Serial y en F4 se suma WebSocket.

Implementación: [`src/protocol.cpp`](../../src/protocol.cpp), parámetros en
[`src/params.cpp`](../../src/params.cpp). Verificación con el robot conectado:
[`hmi/tools/protocol_check.py`](../../hmi/tools/protocol_check.py).

---

## 1. Transporte Serial

| | |
|---|---|
| Velocidad | **921600** baudios, 8N1 (`SERIAL_BAUD` en `config.h`) |
| Trama | Una línea por mensaje, terminada en `\n`. UTF-8 |
| Robot → PC | Las líneas que empiezan con `{` son mensajes JSON. **Todo lo demás es texto libre** para humanos (logs de calibración, prueba de zona muerta, ayuda) y el gateway lo trata como log |
| PC → robot | Una línea que empieza con `{` es un comando JSON hasta `\n` (máx. 1023 bytes). Cualquier otro carácter suelto es una tecla del monitor serie (`d`, `c`, `t`, `j`, `e`, `a`, `p`, `?`) |

Al arrancar, el robot imprime texto de inicio y termina con un mensaje `hello`. Por defecto la
telemetría sale **en texto a 10 Hz** para que el monitor serie siga siendo legible. El gateway, al
conectar, envía `{"type":"cmd","cmd":"tel","fmt":"json","div":1}`.

Durante la prueba de zona muerta (`deadband`) cualquier byte que llegue por el Serial la aborta,
incluido un comando JSON, que se pierde. El gateway no debe enviar nada hasta recibir el `ack`.

---

## 2. Mensajes del robot

### `hello`

Al arrancar y como respuesta a `hello`.

```json
{"type":"hello","fw":"0.2.0","proto":1,"period_ms":20,"structure":"SpeedOuter","params_src":"nvs"}
```

| Campo | Significado |
|---|---|
| `fw` | Versión del firmware (`FW_VERSION`) |
| `proto` | Versión de este protocolo. El gateway debe rechazar una versión que no conoce |
| `period_ms` | Período del lazo de control |
| `params_src` | `nvs` = parámetros guardados; `factory` = valores de `config.h` |

### `tel` — telemetría

Una trama por ciclo de control (50 Hz), de las que se envía 1 de cada `div`.

```json
{"type":"tel","seq":1024,"t":20480,"ang":0.53,"ref":-0.21,"w":-4.2,"pwm":-12.4,"pwmM":-26.1,"rpmL":0.0,"rpmR":-2.9,"kp":70.00,"dt":0.0200,"st":"ACTIVE"}
```

| Campo | Unidad | Significado |
|---|---|---|
| `seq` | — | Número de ciclo de control. Un salto distinto de `div` indica tramas perdidas |
| `t` | ms | `millis()` del robot al tomar la muestra |
| `ang` | ° | Ángulo medido. Negativo = inclinado hacia adelante |
| `ref` | ° | Ángulo deseado (lo fija el lazo de velocidad en `SpeedOuter`) |
| `w` | °/s | Velocidad angular del giroscopio |
| `pwm` | −255…255 | Salida del controlador |
| `pwmM` | −255…255 | PWM aplicado al motor, tras compensar la zona muerta |
| `rpmL`, `rpmR` | RPM | Velocidad de cada rueda. Positiva = avance. La izquierda no es válida (encoder defectuoso) |
| `kp` | — | Kp actual del lazo de ángulo |
| `dt` | s | Período real del ciclo |
| `st` | — | `ACTIVE` controlando · `IDLE` motores off, esperando vertical · `ESTOP` parada de emergencia |

Con los motores apagados `pwm` y `pwmM` valen 0. Si el Serial no alcanza a vaciar la cola (25
tramas), las nuevas se descartan; el contador sale en el `ack` de `tel` (`dropped`).

### `ack` — respuesta a un comando

```json
{"type":"ack","id":7,"ok":true}
{"type":"ack","id":7,"ok":false,"err":"kd_angle fuera de rango [0.000, 5.000]"}
```

`id` es el del comando (o `null` si el JSON no se pudo leer). Algunos comandos agregan campos (ver
abajo).

### `params`

Respuesta a la tecla `p`. Mismo contenido que el `ack` de `get`.

```json
{"type":"params","params":{"structure":"SpeedOuter","kd_angle":1.25, "...": "..."}}
```

---

## 3. Comandos

Forma general: `{"type":"cmd","id":<número>,"cmd":"<nombre>", ...}`. El `id` lo elige el gateway y
vuelve en el `ack`. Los comandos se atienden de a uno; los bloqueantes (`save`, `calib`, `deadband`)
responden al terminar.

| `cmd` | Campos | Qué hace | Campos extra del `ack` | Tecla |
|---|---|---|---|---|
| `hello` | — | Reenvía `hello` | — | |
| `get` | — | Lee los parámetros activos | `params` | `p` |
| `schema` | — | Describe los parámetros (ver §4) | `schema` | |
| `set` | `params: {clave: valor, ...}` | Aplica uno o varios parámetros. **Todos o ninguno**: si uno falla no se aplica nada | `params` (todos, ya aplicados) | |
| `defaults` | — | Vuelve a los valores de fábrica de `config.h`. No toca NVS | `params` | |
| `save` | — | Guarda los parámetros activos en NVS. **Pausa el control** mientras escribe (los motores se apagan; se re-arma al poner el robot vertical) | — | |
| `calib` | — | Calibra el MPU (robot quieto y vertical). Pausa el control, ~1.5 s | — | `c` |
| `deadband` | — | Prueba de zona muerta. Pausa el control, hasta ~20 s | — | `d` |
| `estop` | — | Parada de emergencia: motores off y sin re-armado | — | `e` |
| `arm` | — | Libera la parada. El control se activa al poner el robot vertical | — | `a` |
| `tel` | `on: bool`, `fmt: "json"\|"text"`, `div: 1…50` (todos opcionales) | Configura la telemetría del Serial | `on`, `fmt`, `div`, `dropped` | `t` (on/off), `j` (JSON 50 Hz / texto 10 Hz) |

Ejemplos:

```json
{"type":"cmd","id":1,"cmd":"set","params":{"kd_angle":1.3,"kp_v":0.04}}
{"type":"cmd","id":2,"cmd":"set","params":{"structure":"AngleOuter"}}
{"type":"cmd","id":3,"cmd":"tel","fmt":"json","div":1}
{"type":"cmd","id":4,"cmd":"save"}
```

La parada de emergencia no se guarda: al reiniciar el robot arranca armado, como antes.

---

## 4. Parámetros

El `ack` de `schema` trae un arreglo con un objeto por parámetro. La HMI construye sus controles
a partir de él, así que los rangos se cambian en un solo lugar (`DESCS` en `params.cpp`).

```json
{"key":"kd_angle","type":"float","min":0,"max":5,"def":1.25,"unit":"","desc":"Acción derivativa del lazo de ángulo."}
{"key":"structure","type":"enum","options":["SpeedOuter","AngleOuter"],"def":"SpeedOuter","unit":"","desc":"..."}
```

| `type` | Valor JSON |
|---|---|
| `float` | número |
| `int` | entero |
| `bool` | `true` / `false` |
| `enum` | texto, uno de `options` |
| `sign` | `-1` o `1` |

| Clave | Tipo | Rango | Fábrica | Constante en `config.h` |
|---|---|---|---|---|
| `structure` | enum | SpeedOuter / AngleOuter | SpeedOuter | `CONTROL_STRUCTURE` |
| `setpoint_angle` | float | −10 … 10 ° | 0 | `SETPOINT_ANGLE` |
| `kp_min` | float | 0 … 150 | 70 | `KP_MIN` |
| `kp_max` | float | 0 … 150 | 70 | `KP_MAX` |
| `kd_angle` | float | 0 … 5 | 1.25 | `KD_ANGLE` |
| `ki_angle` | float | 0 … 10 | 1.0 | `KI_ANGLE` |
| `kp_speed` | float | 0 … 3 | 0.8 | `KP_SPEED` |
| `ki_speed` | float | 0 … 2 | 0 | `KI_SPEED` |
| `kp_v` | float | 0 … 0.5 °/RPM | 0.05 | `KP_V` |
| `ki_v` | float | 0 … 0.5 °/(RPM·s) | 0.03 | `KI_V` |
| `speed_sign` | sign | ±1 | −1 | `SPEED_LOOP_SIGN` |
| `speed_ref` | float | −30 … 30 RPM | 0 | `SPEED_REF_RPM` |
| `max_tilt_ref` | float | 0 … 10 ° | 4 | `MAX_TILT_REF_DEG` |
| `pwm_deadband` | float | 0 … 40 | 14 | `PWM_DEADBAND` |
| `nn_error_band` | float | 0 … 5 ° | 1.0 | `NN_ERROR_BAND` |
| `nn_lr` | float | 0 … 3 | 0.5 | `NN_LEARNING_RATE` |
| `kp_tau` | float | 0 … 2 s | 0.3 | `KP_FILTER_TAU` |
| `max_angle` | float | 10 … 60 ° | 40 | `MAX_ANGLE` |
| `rearm_angle` | float | 1 … 15 ° | 5 | `REARM_ANGLE` |
| `mpu_dlpf` | int | 0 … 6 | 3 | `MPU_DLPF_MODE` |
| `gyro_deriv` | bool | — | true | `USE_GYRO_DERIVATIVE` |

Reglas entre parámetros: `kp_min ≤ kp_max` y `rearm_angle < max_angle`.

Cuándo se aplican:

- Todos se aplican en el **siguiente ciclo de control** (≤ 20 ms). La tarea copia el juego completo
  al empezar el ciclo, así que nunca mezcla valores viejos y nuevos.
- `structure`: si el robot está controlando, el controlador se reinicia (integrales y referencia).
- `mpu_dlpf`: la propia tarea de control reescribe el registro del MPU antes de leerlo.
- `kp_min` / `kp_max`: Kp llega al nuevo valor con el filtro de `kp_tau`, no de golpe.

### Persistencia

`save` guarda el juego activo en NVS (namespace `params`). Al arrancar se cargan de NVS si existen
y pasan la validación; si no, se usan los de `config.h`. Si se cambia la estructura `Params`, hay
que subir `PARAMS_LAYOUT_VERSION` en `params.cpp`: lo guardado con otra versión se ignora.

Para volver a fábrica de forma permanente: `defaults` y luego `save`.

---

## 5. Versionado

- Agregar un campo a un mensaje o un comando nuevo **no** cambia la versión: el gateway ignora lo
  que no conoce.
- Cambiar el significado o el nombre de un campo existente sube `PROTOCOL_VERSION`.
