# HMI del balancín

Laboratorio web para monitorear, sintonizar y registrar el robot. Plan y fases en
[docs/hmi/PLAN.md](../docs/hmi/PLAN.md).

```
robot ──USB 921600 ─────┐
      └─WiFi WebSocket ─┴► gateway (Python, FastAPI) ──WebSocket──► HMI web (PC / tablet / celular)
                               └─ SQLite: sesiones, telemetría, eventos
```

| Carpeta | Qué es | Documentación |
|---|---|---|
| `gateway/` | Enlace con el robot, API y base de datos | [GATEWAY.md](../docs/hmi/GATEWAY.md) |
| `web/` | Interfaz (TypeScript + Vite, sin framework) | este archivo |
| `tools/` | `protocol_check.py` (verificación del protocolo, por USB o `--ws`), `link_bench.py` (medición de enlaces) | [PROTOCOLO.md](../docs/hmi/PROTOCOLO.md) |
| `iniciar.sh` | Arranque en un comando (instala, compila, arranca) | este archivo |
| `Dockerfile`, `docker-compose.yml`, `mosquitto/` | Imagen del laboratorio (gateway + HMI) y broker MQTT | este archivo |

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

Un solo comando (la primera vez instala y compila lo necesario; después sólo arranca):

```bash
hmi/iniciar.sh                    # HMI accesible desde la red (PC, tablet, celular)
hmi/iniciar.sh --clave una-clave  # con clave de acceso
hmi/iniciar.sh --mqtt             # y además el broker MQTT (Mosquitto)
hmi/iniciar.sh --solo-este-pc     # sólo en este equipo
```

Equivale a lo manual:

```bash
cd hmi/gateway
.venv/bin/python -m app [--host 0.0.0.0] [--clave X]
```

Abrir <http://127.0.0.1:8000>, elegir el transporte y **Conectar**:

- **USB**: el puerto del robot (el adaptador USB sale primero).
- **WiFi** (fw ≥ 0.4.0): ver abajo.
- **MQTT** (fw ≥ 0.5.0, opcional): ver abajo.
- **Simulado**: un robot simulado dentro del gateway, para probar sin hardware.

### Sin cable (WiFi)

Al encender, el robot crea su red **`Balancin-XXXX`** con una clave propia de ese robot
(**`bal-xxxxxx`**). Para saberla: tarjeta **WiFi del robot** (conectado por USB), tecla `w` en el
monitor serie, o el mensaje de arranque.

1. Unir el PC a la red del robot. (El PC pierde internet mientras tanto, salvo que tenga cable de red.)
2. Arrancar el gateway: `.venv/bin/python -m app --host 0.0.0.0`.
3. En la HMI: **WiFi**, dirección `192.168.4.1`, **Conectar**.
4. El celular o la tablet también se unen a la red del robot y abren la dirección de «HMI en la red»
   que imprime el gateway (la IP del PC en esa red, p. ej. `http://192.168.4.2:8000`).

Para usar la red del laboratorio en vez de la del robot: **WiFi del robot → Cambiar la red → Red
local**, red y clave, **Guardar y aplicar**. El robot se une a esa red (si no puede en 15 s, vuelve a
la suya) y se conecta con su IP o su nombre `balancin-xxxx.local`. PC y celular siguen con internet.
Las redes con portal de acceso o WPA2-Enterprise (como la universitaria) no sirven para el robot.

Si el robot se apaga o sale de alcance, el gateway reintenta solo cada 2 s; **Cancelar reconexión**
lo detiene.

**Varias pantallas: usar un router.** En la red propia del robot, todo lo que la tablet o el celular
reciben del PC pasa por el ESP32, que es un punto de acceso muy limitado: la página tarda o no
termina de cargar y la conexión del PC con el robot puede cortarse. La red propia sirve bien para
**un** equipo (el PC). Para PC + tablet + celular, poner el robot en **Red local** con un router
(el del laboratorio, uno de viaje, o el de casa): el router hace ese trabajo y todos quedan en la
misma red.

**Si el celular o la tablet no abren la HMI** en la red del robot (la página queda cargando y en el
log del gateway no aparece ninguna petición de `192.168.4.x`): el equipo probablemente volvió solo a
otra red con internet, porque la del robot no tiene. En iOS: Ajustes → Wi-Fi → (i) de la red de casa
o del laboratorio → desactivar **Conexión automática** mientras se usa el robot, y confirmar que el
equipo tiene una IP `192.168.4.x`.

Verificar el protocolo por WiFi: `hmi/gateway/.venv/bin/python hmi/tools/protocol_check.py --ws 192.168.4.1`.

- **Desde el celular** (misma red WiFi que el PC):
  1. Arrancar con `.venv/bin/python -m app --host 0.0.0.0`. Sin esa opción el gateway sólo acepta
     conexiones del propio PC.
  2. Abrir en el celular la dirección que el gateway imprime al arrancar (`HMI en la red:`), con el
     puerto: `http://192.168.x.x:8000`. La misma dirección aparece en el panel **Conexión**.
     `127.0.0.1` en el celular es el propio celular, no el PC.
  3. Si macOS pregunta si Python puede aceptar conexiones entrantes, **Permitir**. Si no abre,
     revisar Ajustes → Red → Firewall, y que la red no sea de invitados (suelen aislar equipos).
  4. No hay autenticación: hacerlo sólo en una red de confianza.
- Las advertencias `Invalid HTTP request received` en el log son intentos de Safari de abrir la
  página por HTTPS; no afectan.
- **Parada de emergencia**: botón rojo o tecla `Esc` (en el PC).
- **Clave de acceso** (opcional): con `--clave` la HMI la pide una vez por equipo (se recuerda 30
  días; **Salir**, al pie, la olvida). Sin clave, cualquiera en la red puede mover el robot.
- **Celular**: barra de Seguridad fija arriba y cuatro pestañas abajo (En vivo, Historial, Ajustes,
  Conexión). Se puede **agregar a la pantalla de inicio** (Safari: Compartir → Agregar a inicio;
  Chrome: menú → Instalar app) y se abre como una app, sin la barra del navegador.
- **Parámetros**: con **En vivo** cada cambio se envía al soltar el slider o confirmar el número
  (Enter). Sin **En vivo** los cambios se acumulan (borde dorado) y se envían juntos con
  **Aplicar**, útil para mover `kp_min` y `kp_max` a la vez. ↺ vuelve al valor de fábrica de ese
  parámetro; el punto • junto al nombre indica que el robot no está en el valor de fábrica.
  **Guardar en el robot** los deja en su memoria (pausa el control un instante: sujetar el robot).
- **Juegos de parámetros**: se guardan en la base del PC. **Cargar** muestra antes qué cambia;
  **Exportar** / **Importar JSON** sirven para compartirlos entre equipos.
- El monitor serie de PlatformIO y el gateway no pueden usar el puerto a la vez.
- Los datos quedan en `hmi/gateway/data/balancin.db` (fuera de git). Cómo revisarlos:
  [GATEWAY.md §5](../docs/hmi/GATEWAY.md#5-revisar-la-base-de-datos).

### MQTT (opcional)

Alternativa al WiFi directo, con un broker en medio. Pensada para despliegues con varios robots o un
servidor remoto; en el aula el WiFi directo es más simple (ver la comparación en `docs/hmi/PLAN.md`).

1. Broker en el PC: `docker compose -f hmi/docker-compose.yml up -d mosquitto`, o sin Docker
   `mosquitto -c hmi/mosquitto/mosquitto-local.conf` (Homebrew: `brew install mosquitto`).
2. Robot conectado (USB o WiFi) → **WiFi del robot → MQTT** → activar, **Este PC** (IP del broker)
   → **Guardar**. El robot necesita estar en una red donde vea al PC.
3. **Conexión → MQTT**: broker `127.0.0.1` (el gateway corre en el mismo PC), **Buscar**, elegir
   el robot, **Conectar**.

### Medir un enlace

Con el gateway desconectado del robot:

```bash
hmi/gateway/.venv/bin/python hmi/tools/link_bench.py --serial /dev/cu.wchusbserial...
hmi/gateway/.venv/bin/python hmi/tools/link_bench.py --ws 192.168.4.1
hmi/gateway/.venv/bin/python hmi/tools/link_bench.py --mqtt 192.168.1.7 --robot balancin-b884
```

Ida y vuelta de un comando (p50/p95), telemetría recibida, tramas perdidas y jitter de llegada.

## Equipo siempre encendido (Docker)

`hmi/Dockerfile` compila la HMI y la empaqueta con el gateway; `hmi/docker-compose.yml` levanta el
gateway (puerto 8000) y el broker MQTT (1883). Para un Linux del laboratorio o un servidor:

```bash
cd hmi
echo "BALANCIN_CLAVE=una-clave" > .env   # opcional
docker compose up -d
```

- La base de datos queda en el volumen `gateway-data` (sobrevive a reinicios y actualizaciones).
- Robot por **WiFi** (su IP en la red local) o **MQTT** (broker `mosquitto` desde la HMI).
- USB sólo en Linux, descomentando `devices:` en el compose. Docker Desktop (macOS/Windows) no da
  acceso a puertos USB: en el Mac usar `iniciar.sh`.

## Actualizar

Tras `git pull` (o cualquier cambio en `hmi/`): **reiniciar `hmi/iniciar.sh`** (recompila la HMI si
cambió). A mano: `npm run build` en `hmi/web` y reiniciar el gateway. Con Docker:
`docker compose up -d --build`. El gateway sirve la HMI desde el disco, así que una página nueva con un gateway viejo
falla en las funciones nuevas; la HMI lo avisa en el registro («El gateway es v… y la HMI v…»).
Las versiones están en `hmi/gateway/app/__init__.py` y `HMI_VERSION` en `hmi/web/src/main.ts`, y se
suben juntas.

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
| `core/paramspec.ts` | Presentación de los parámetros: nombre legible, grupo, a qué estructura aplican, paso del slider, validación local |
| `ui/` | Tema, conexión (se pliega al conectar en tablet y celular), cifras, registro, `params.ts` (panel de parámetros), `paramsets.ts` (juegos), `commands.ts` (calibrar, zona muerta), `dialog.ts` (confirmaciones) |
| `plots/` | Trazas con uPlot y leyenda propia; modo en vivo (ventana de 5–60 s) y modo reproducción (sesión completa, cabezal, cursor sincronizado con la escena) |
| `scene/` | Escena del robot portada del Simulador_Balancin: `scene.ts` (2D y 3D en canvas), `pose.ts` (telemetría → pose; integra la posición desde las RPM), `geometry.ts` (medidas del robot para dibujarlo) |
| `ui/history.ts` | Historial: lista de sesiones, gráficas reproducibles, tabla paginada, eventos, CSV, notas y borrado |
| `styles/` | `tokens.css` (colores del Simulador_Balancin + paleta de series), `base.css`, `layout.css` |

### Estructura de `gateway/app`

| Módulo | Contenido |
|---|---|
| `main.py` | Rutas REST y WebSocket; sirve `web/dist` si existe |
| `robot.py` | `RobotLink`: configuración del robot, comandos con ack, sesiones, reparto de telemetría |
| `hub.py` | Clientes WebSocket conectados |
| `transports/` | `serial_port.py`, `websocket.py` (WiFi), `mqtt.py`, `demo.py` (robot simulado) |
| `storage/db.py` | SQLite: sesiones, telemetría, eventos, juegos de parámetros |

### Tamaños de pantalla

Probados: escritorio (1440 × 860), iPad mini horizontal (1133 × 690) y vertical (744 × 1060), celular (375 px).

| Ancho | Disposición |
|---|---|
| > 1080 px | La página ocupa el alto de la pantalla y no se desplaza. Izquierda (monitoreo): cifras y la vista con pestañas **En vivo** (escena + trazas lado a lado) e **Historial**. Derecha (control): **Seguridad fija arriba** y debajo conexión, parámetros, juegos, comandos y registro con su propio desplazamiento |
| 701–1080 px | Monitoreo arriba (~60 % del alto) y control abajo, cada uno con su desplazamiento; seguridad a todo el ancho y las demás tarjetas en dos columnas |
| ≤ 700 px | Seguridad fija arriba y pestañas abajo: **En vivo** (cifras, escena, trazas), **Historial**, **Ajustes** (WiFi, parámetros, juegos, comandos), **Conexión** (conexión, registro) |

En pantallas bajas (≤ 820 px de alto) el encabezado oculta la descripción para dar alto a la vista.
En pantallas táctiles los controles miden al menos ~40 px de alto.

### Escena

- **2D** (la que mide): θ contra la vertical punteada; línea ámbar = θ ref; arco azul sobre la
  rueda = PWM aplicado (sentido y magnitud); regla del piso cada 5 cm. **Centrar** pone el eje en 0.
- **3D**: arrastrar (ratón o un dedo) gira la cámara; la rueda del ratón, dos dedos en el trackpad o
  **pellizcar con dos dedos** en la tablet acercan; **Reencuadrar** vuelve al inicio.
- La posición no viene en la telemetría: se integra la media de las RPM de las dos ruedas (si el
  encoder izquierdo falla, la posición deriva).
- Las medidas del robot dibujado salen del simulador; para que se parezca al prototipo, corregirlas
  en `web/src/scene/geometry.ts`.

### Historial

Pestaña **Historial**: lista de sesiones (buscar por notas, firmware o número; filtrar por
transporte). En la sesión elegida:

- **Gráficas**: la sesión completa. Pasar el cursor por las trazas mueve la escena a ese instante;
  ▶ reproduce a 0.25–4×; arrastrar sobre las trazas acerca, doble clic vuelve.
- **Tabla**: todas las tramas, 100 por página; **Ir al cabezal** salta al instante de la escena.
- **Eventos**: comandos y mensajes del robot; un clic lleva a ese instante.
- **Notas** (Enter guarda), **CSV** y **Borrar**.

### Colores de las trazas

Las series usan una paleta categórica en orden fijo (`--series-1..5` en `tokens.css`), validada para
daltonismo en tema claro y oscuro. Los colores institucionales (`--azul`, `--rojo`, ...) quedan para
la interfaz: son demasiado oscuros para líneas de datos. Cada gráfico tiene una sola magnitud y un
solo eje y.
