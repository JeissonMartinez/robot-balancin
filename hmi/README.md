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
| `transports/` | `serial_port.py`, `demo.py` (robot simulado); en F4 WebSocket, en F5 MQTT |
| `storage/db.py` | SQLite: sesiones, telemetría, eventos, juegos de parámetros |

### Tamaños de pantalla

Probados: escritorio (1440 × 860), iPad mini horizontal (1133 × 690) y vertical (744 × 1060), celular (375 px).

| Ancho | Disposición |
|---|---|
| > 1080 px | La página ocupa el alto de la pantalla y no se desplaza. Izquierda (monitoreo): cifras y la vista con pestañas **En vivo** (escena + trazas lado a lado) e **Historial**. Derecha (control): **Seguridad fija arriba** y debajo conexión, parámetros, juegos, comandos y registro con su propio desplazamiento |
| 701–1080 px | Monitoreo arriba (~60 % del alto) y control abajo, cada uno con su desplazamiento; seguridad a todo el ancho y las demás tarjetas en dos columnas |
| ≤ 700 px | Una columna con desplazamiento normal: seguridad, conexión, cifras, vista, parámetros, juegos, comandos, registro |

En pantallas bajas (≤ 820 px de alto) el encabezado oculta la descripción para dar alto a la vista.
En pantallas táctiles los controles miden al menos ~40 px de alto.

### Escena

- **2D** (la que mide): θ contra la vertical punteada; línea ámbar = θ ref; arco azul sobre la
  rueda = PWM aplicado (sentido y magnitud); regla del piso cada 5 cm. **Centrar** pone el eje en 0.
- **3D**: arrastrar gira la cámara, la rueda del ratón acerca, **Reencuadrar** vuelve al inicio.
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
