# 📚 Documentación Técnica Completa – Balancín-ControlRN

## 1. Visión general del sistema

El proyecto **Balancín-ControlRN** implementa el control de un **robot balancín** (péndulo invertido sobre ruedas) sobre ESP32-S3, combinando:

- Medición de ángulo con **MPU6050**.
- Estimación de ángulo mediante **filtro complementario**.
- Medición de velocidad de ruedas con **encoders incrementales**.
- Control **en cascada** (ángulo → velocidad → PWM).
- Una **red neuronal feed-forward** que ajusta en línea la ganancia proporcional del lazo de ángulo.
- Arquitectura basada en **FreeRTOS** con una tarea de control periódica.

El objetivo es mantener el robot en equilibrio vertical. El seguidor de línea de versiones anteriores fue retirado.

---

## 2. Arquitectura de software 🧱

Cada módulo tiene su interfaz en `include/*.h` y su implementación en `src/*.cpp`. El estado interno de cada módulo es `static` (privado); sólo se exportan funciones. Los parámetros de sintonía viven en `params.cpp` y llegan al controlador como `const Params &` en cada ciclo.

### 2.1. Módulos principales

- `src/main.cpp`
  - Inicializa Serial (921600), parámetros (`paramsInit()`), motores (deshabilitados), MPU6050 (verifica conexión), calibración, encoders y red neuronal.
  - Arranca la tarea de control con `startControlTask()` y envía `hello`.
  - `loop()`: botón de calibración (con antirrebote), lectura del Serial (teclas sueltas o líneas JSON hacia `protocolHandleLine()`) y vaciado de la cola de telemetría.

- `config.h`
  - Pines de motores, encoders, I2C y botón.
  - PWM (20 kHz, 8 bits, canales/timers LEDC), `CPR`, `GEAR_RATIO`, `PPR_WHEEL`.
  - Control: `CONTROL_PERIOD_MS`, `PWM_LIMIT`, `SERIAL_BAUD`, `FW_VERSION`, `PROTOCOL_VERSION`.
  - Valores de fábrica de todos los parámetros ajustables (`KP_MIN`, `KD_ANGLE`, `KP_V`, `MAX_ANGLE`, ...).

- `params.h / params.cpp`
  - Estructura `Params` activa en RAM (spinlock), `paramsSnapshot()` devuelve copia y versión.
  - Tabla de descriptores `DESCS` (clave, tipo, rango, unidad): fuente única para validar y para el comando `schema`.
  - `paramsApplyJson()` todo o nada con reglas cruzadas; `paramsSave()` / `paramsInit()` con NVS (namespace `params`, `PARAMS_LAYOUT_VERSION`).

- `protocol.h / protocol.cpp`
  - Independiente del transporte: interpreta una línea JSON y responde en un `Print`.
  - Formato de telemetría JSON y texto. Especificación en `docs/hmi/PROTOCOLO.md`.

- `encoders.h / encoders.cpp`
  - ISRs en IRAM con lectura directa de registros GPIO (seguras durante escrituras a NVS).
  - Contadores protegidos con spinlock: `readAndResetEncoders()` es atómico.
  - `updateWheelRPM()` (RPM + filtro IIR) y `resetWheelRPM()`.

- `motors.h / motors.cpp`
  - `setupMotors()`, `driveMotorsDifferential(pwmL, pwmR)`, `compensateDeadband(pwm, deadband)`, `enableMotors()`, `stopMotors()`.

- `mpu_block.h / mpu_block.cpp`
  - `setupMPU(dlpf)`, `setMPUFilter(dlpf)`, `loadCalibration()`, `calibrateMPU()`, `resetAngleFromAccel()`, `updateAngle(dt)`.

- `nn_cascade_block.h / nn_cascade_block.cpp`
  - `initNeural(p)`, `resetCascade(p, angle)`, `cascada(p, angle, rate, rpmL, rpmR, dt)`, `getAngleReference()`, `getKpAngle()`.

- `tasks_block.h / tasks_block.cpp`
  - `TaskBalanceo`, `pauseControl()`, `resumeControl()`, `runWithControlPaused()`, `setEStop()`, `receiveTelemetry()` (cola de 25 tramas).

- `lib/Neural_Networks_FF`, `lib/Dynamic_Array`
  - Librerías locales (no están en el registro de PlatformIO).

---

## 3. Modelado del robot balancín 📐

> Nota: El firmware no resuelve explícitamente el modelo matemático completo del péndulo invertido, pero el diseño de la cascada y la RN están inspirados en ese comportamiento.

### 3.1. Variables principales

- \( \theta \): ángulo del robot respecto a la vertical (grados).
- \( \dot{\theta} \): velocidad angular (giroscopio, eje X).
- \( v_L, v_R \): velocidades de las ruedas izquierda y derecha (RPM).
- \( v \): velocidad media del robot (media de ambas ruedas).

### 3.2. Encoders → RPM

- Interrupción `CHANGE` en el canal A de cada encoder; el sentido se obtiene comparando A con B.
- `PPR_WHEEL = CPR · GEAR_RATIO = 44 · 119` pulsos por vuelta de rueda.
- \( \Delta N_L, \Delta N_R \): pulsos acumulados en el período \( \Delta t \).

RPM de cada rueda:

- \( \text{RPM}_L = \dfrac{\Delta N_L}{\text{PPR}} \cdot \dfrac{60}{\Delta t} \), análogo para \( \text{RPM}_R \).

Velocidad media:

- \( v = \dfrac{\text{RPM}_L + \text{RPM}_R}{2} \)

Filtro IIR (pasa‑bajas):

- \( \text{RPM}_{f}(k) = 0.7 \cdot \text{RPM}_{f}(k-1) + 0.3 \cdot \text{RPM}(k) \)

---

## 4. Estimación de ángulo con MPU6050 🎛️

Escalas: acelerómetro 16384 LSB/g (±2 g), giroscopio 131 LSB/(°/s) (±250 °/s). A cada lectura se le restan los offsets de calibración.

Filtro pasa-bajas digital interno (DLPF) del MPU6050 configurado a 42 Hz (`MPU_DLPF_MODE` = 3). Con el valor por defecto (256 Hz), el giroscopio registraba la vibración de los motores y los golpes del juego de la reductora, y la acción derivativa los amplificaba hasta producir una oscilación sostenida con el PWM saturado.

### 4.1. Ángulo por acelerómetro

- \( \theta_{\text{acc}} = \arctan2(a_y, a_z) \)

Buena a baja frecuencia, pero ruidosa.

### 4.2. Ángulo por giroscopio

- \( \theta_{\text{gyro}}(k) = \theta_{\text{gyro}}(k-1) + \omega_x(k) \cdot \Delta t \)

Buena a alta frecuencia, pero con deriva.

### 4.3. Filtro complementario

- \( \theta_{\text{filt}}(k) = 0.98 \left[ \theta_{\text{filt}}(k-1) + \omega_x(k) \cdot \Delta t \right] + 0.02\, \theta_{\text{acc}}(k) \)

Al arrancar y tras calibrar, \( \theta_{\text{filt}} \) se inicializa con \( \theta_{\text{acc}} \) (`resetAngleFromAccel()`).

---

## 5. Control en cascada ⚙️

Hay dos estructuras seleccionables con el parámetro `structure` (fábrica: `CONTROL_STRUCTURE` en `config.h`). Los parámetros de ambas se conservan, así que se puede cambiar de una a otra en ejecución; si el robot está controlando, el controlador se reinicia al cambiar. La estructura activa se imprime al arrancar.

### 5.0. `SpeedOuter`: estructura estándar de balancín (activa por defecto)

1. **Lazo externo de velocidad (PI, 10 Hz)**: mide la velocidad media de la rueda en cada periodo y fija el ángulo deseado.
   - \( \theta_{ref} = \theta_0 + \text{sat}_{\pm 4°}\left(s\,(K_p^v\,v + K_i^v \int v\,dt)\right) \), con `SPEED_LOOP_SIGN` \( s = -1 \), `Kp_v` = 0.05 °/RPM y `Ki_v` = 0.03 °/(RPM·s).
   - Convenio de signos: PWM > 0 mueve el robot hacia adelante y corrige ángulos negativos, así que ángulo < 0 significa inclinado hacia adelante.
   - Por qué \( s = -1 \): los motorreductores (1:119) se comportan casi como fuentes de velocidad. Con el lazo interno PD, en régimen permanente el robot avanza a unos 15 RPM por cada grado de `angle_ref` por encima del punto de equilibrio. Con \( s = +1 \) (primera versión), avanzar subía `angle_ref`, lo que lo hacía avanzar más: realimentación positiva con ganancia ≈ 15 · 0.10 = 1.5. El robot equilibraba al arrancar y luego se alejaba cada vez más rápido.
   - El término integral devuelve el robot a su posición inicial y absorbe la diferencia entre el cero calibrado y el punto de equilibrio real (centro de masa).
2. **Lazo interno de ángulo (PD, 50 Hz)**: \( u = 0.8\,(K_p^\theta e + K_d^\theta \dot e) \), con \( e = \theta_{ref} - \theta \). El factor 0.8 (`ANGLE_LOOP_PWM_GAIN`) hace que `KP_MIN`/`KP_MAX` y `Kd_angle` den la misma ganancia efectiva que en la estructura original.
3. La red neuronal ajusta Kp del lazo interno exactamente igual que en la estructura original.

Si el robot se aleja cada vez más rápido y `Ref` se queda en ±4°, el signo del lazo externo está invertido: cambia `SPEED_LOOP_SIGN`. Si oscila lento adelante-atrás (periodo de 1-3 s), baja `Ki_v` y luego `Kp_v`.

### `AngleOuter`: estructura original

En esta estructura el lazo de ángulo genera una referencia de velocidad de hasta ±300 RPM, pero la rueda solo alcanza unas 15-20 RPM medidas. El lazo de velocidad no llega a seguir su referencia, así que en la práctica funciona como un PD de ángulo directo al PWM, y nada corrige la deriva de posición.

1. **Lazo de ángulo (externo, PID)**: medida \( \theta_{\text{filt}} \), referencia `setpoint_angle` (0°), salida = referencia de velocidad.
2. **Lazo de velocidad (interno, PI)**: medida \( v \), referencia = salida del lazo de ángulo, salida = PWM.

### 5.1. PID de ángulo

- \( e_\theta(k) = \theta_{\text{ref}} - \theta_{\text{filt}}(k) \)
- \( v_{\text{ref}}(k) = \text{sat}_{\pm 300}\left( K_p^\theta e_\theta(k) + K_i^\theta \sum e_\theta \Delta t + K_d^\theta \dfrac{e_\theta(k) - e_\theta(k-1)}{\Delta t} \right) \)

- \( K_p^\theta \) la calcula la red neuronal en cada ciclo.
- Término derivativo: con `USE_GYRO_DERIVATIVE = true` (valor actual) se usa \( -\omega_x \) del giroscopio en lugar de diferenciar el error, lo que reduce ruido y retardo.
- Anti-windup condicional: no se integra si la salida está saturada y el error empuja en el mismo sentido.

### 5.2. PI de velocidad

- \( e_v(k) = v_{\text{ref}}(k) - v(k) \)
- \( u(k) = \text{sat}_{\pm PWM\_LIMIT}\left( K_p^v e_v(k) + K_i^v \sum e_v \Delta t \right) \)

- \( v \) es la media de ambas ruedas, o sólo la rueda derecha si `USE_LEFT_ENCODER = false` (valor actual: el encoder izquierdo está defectuoso).
- Anti-windup condicional y \( \sum e_v \Delta t \) limitado a ±150.
- `u` se aplica igual a ambos motores, después de compensar la zona muerta (sección 5.4).

### 5.4. Compensación de zona muerta

La prueba `d` (monitor serie) midió que la rueda derecha empieza a girar con PWM ≈ 22-26, con el robot apoyado en el suelo. Por debajo de ese valor el motor no vence la fricción ni el juego de la reductora: el robot se quedaba ligeramente inclinado hasta que el motor arrancaba de golpe.

\( u_{motor} = \text{signo}(u)\left(DB + |u|\,\frac{255 - DB}{255}\right) \) para \( |u| \ge B \), con interpolación lineal desde 0 para \( |u| < B \).

- `PWM_DEADBAND` (DB) = 14: algo menos que lo medido, porque arrancar desde parado cuesta más que mantener el giro y compensar de más produce oscilación.
- `PWM_DEADBAND_BLEND` (B) = 4: evita el salto en 0, para que el ruido en reposo no se convierta en golpes de ±DB.
- La telemetría muestra `u` (salida del controlador) en `pwm` y el PWM compensado en `pwmM` (sólo en JSON).

### 5.3. Valores de fábrica

Se ajustan en ejecución; la tabla completa con claves y rangos está en `docs/hmi/PROTOCOLO.md` §4.

| Parámetro | Valor |
|---|---|
| `Ki_angle`, `Kd_angle` | 1.0, 1.25 |
| `Kp_speed`, `Ki_speed` | 0.8, 0 (integral desactivado hasta verificar encoders) |
| `PWM_LIMIT` | 255 |

---

## 6. Red neuronal feed-forward 🧠

### 6.1. Estructura

- Topología **3‑3‑1**: 3 entradas, 3 neuronas ocultas, 1 salida.
- Activaciones: `logsig`, `logsig`, `poslin_lim` (límites 0..1).
- Entradas (normalizadas /100): \( [e_\theta(k),\; e_\theta(k)-e_\theta(k-1),\; e_\theta(k-1)-e_\theta(k-2)] \).
- Salida: \( y \in [0, 1] \Rightarrow K_p^\theta = K_{p,\min} + (K_{p,\max} - K_{p,\min})\,y \), con `kp_min`/`kp_max` (fábrica 70/70: Kp fija). Kp nunca es negativa. Como el PWM ≈ 0.8·Kp·e, un rango de 55..85 equivale a 44..68 PWM/°, coherente con la sintonía manual previa (Kp 50-70 estable, 80 oscilante).

### 6.2. Funciones de activación

- `logsig`: \( \dfrac{1}{1 + e^{-z}} \)
- `poslin_lim`: \( z \) acotado a \( [\text{mín}, \text{máx}] = [0, 1] \)

### 6.3. Entrenamiento en línea

En cada ciclo, `TRAIN_NET_ONLINE` actualiza los pesos con el error de entrenamiento:

- \( E = \dfrac{|e_\theta| - \text{NN\_ERROR\_BAND}}{100} \), con `NN_ERROR_BAND` = 1.0°.
- \( E > 0 \) (el robot se aleja de la vertical más que la banda) → la salida sube → Kp sube.
- \( E < 0 \) (dentro de la banda) → Kp baja hacia `KP_MIN`, suavizando la respuesta.
- `LearningRate` = `NN_LEARNING_RATE` = 0.5. Con 2.25 los pesos crecían hasta que la red conmutaba Kp entre el mínimo y el máximo en un solo ciclo, y eso producía vibración.
- La Kp que propone la red pasa por un filtro pasa-bajas de primer orden (`KP_FILTER_TAU` = 0.3 s) antes de aplicarse: \( K_p(k) = K_p(k-1) + \frac{\Delta t}{\tau + \Delta t}\,(K_p^{RN} - K_p(k-1)) \).

Nota de la librería: en `TRAIN_NET_ONLINE` la variable `epochs` vale siempre 1, por lo que la tasa adaptativa por peso `h` permanece en 1 y no se aplica momento.

El entrenamiento sólo se ejecuta mientras el control está activo (robot dentro de `MAX_ANGLE`). Los pesos se conservan al reiniciar el controlador tras una caída; sólo se reinician integradores e historial de errores.

---

## 7. Tarea FreeRTOS y flujo 🧵⏱️

### 7.1. TaskBalanceo

Núcleo 1, prioridad 3, período `CONTROL_PERIOD_MS` = 20 ms (50 Hz) con `vTaskDelayUntil`.

1. Copiar los parámetros activos (`paramsSnapshot`). Si cambió `mpu_dlpf`, reescribir el registro del MPU; si cambió `structure`, reiniciar el controlador.
2. Medir \( \Delta t \) real con `esp_timer_get_time()`.
3. `updateAngle(dt)`: leer MPU6050 y aplicar filtro complementario.
4. `updateWheelRPM(dt, ...)`: leer/resetear encoders de forma atómica y filtrar RPM.
5. Máquina de seguridad:
   - Activo y (parada de emergencia o \( |\theta| > \) `max_angle`, 40°) → `stopMotors()`, control inactivo.
   - Inactivo, sin parada de emergencia y \( |\theta| < \) `rearm_angle` (5°) → `resetCascade()`, `enableMotors()`, control activo.
   - Al encender, el control empieza inactivo hasta que el robot se pone vertical.
6. Si está activo: `cascada(...)` → `compensateDeadband()` → `driveMotorsDifferential(pwm, pwm)`.
7. Encolar la trama de telemetría (sin esperar; si la cola está llena se descarta y se cuenta).

La tarea no usa Serial; `loop()` vacía la cola y envía 1 de cada `div` tramas (texto a 10 Hz por defecto, JSON a 50 Hz para la HMI).

### 7.2. Calibración de MPU

1. `loop()` detecta la pulsación de `BTN_CAL` (antirrebote de 50 ms; se calibra al soltar).
2. `pauseControl()`: la tarea detiene los motores al inicio de su siguiente ciclo (nunca en medio de una transacción I2C) y confirma.
3. `calibrateMPU()`: 500 muestras, offsets promediados, guardado en NVS.
4. `resumeControl()`: la tarea reinicializa el ángulo con el acelerómetro, descarta pulsos de encoder y vuelve a esperar a estar vertical para activar el control.

---

## 8. Notas de hardware 🔌

- GPIO 3 (`R_A`) y GPIO 46 (`R_B`) son pines de arranque (strapping) del ESP32-S3. En arranque normal no afectan; si hay problemas al programar, desconectar el encoder derecho.
- La librería `Neural_Networks_FF` conmuta GPIO 13 en cada entrenamiento (`digitalWrite(13, ...)`). No usar GPIO 13 para otra función.

---

## 9. Flujo de trabajo del desarrollador 👨‍💻

- Rama principal: `main`.

```
git status
git add .
git commit -m "Descripción del cambio"
git push
```

Compilación y carga con PlatformIO:

```
pio run -t upload
pio device monitor   # 921600 baudios
```

---

## 10. Ideas de mejora 🌱

- HMI web para ajustar parámetros y registrar telemetría: en desarrollo, ver `docs/hmi/PLAN.md`.
- Reincorporar un modo de desplazamiento/giro (referencia de velocidad y giro diferencial aditivo).

---

## 11. Resumen conceptual 🧠

- El robot es un **péndulo invertido sobre ruedas**.
- El lazo de ángulo mantiene la “vara” vertical.
- El lazo de velocidad traduce esa corrección en movimiento de ruedas.
- La red neuronal ajusta \( K_p \) del lazo de ángulo para adaptarse a cambios.
