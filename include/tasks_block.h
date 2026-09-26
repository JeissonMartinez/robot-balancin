/**
 * @file tasks_block.h
 * @brief Tarea FreeRTOS de balanceo, pausa segura, parada de emergencia y telemetría.
 *
 * TaskBalanceo (núcleo 1, prioridad 3, periodo CONTROL_PERIOD_MS):
 *  - Periodo fijo con vTaskDelayUntil y dt medido con esp_timer_get_time().
 *  - Al inicio de cada ciclo copia los parámetros activos (paramsSnapshot). Si cambió
 *    el filtro del MPU lo reescribe; si cambió la estructura reinicia el controlador.
 *  - Ángulo (MPU) + RPM (encoders) → cascada() → mismo PWM a ambos motores.
 *  - Seguridad: si |ángulo| > maxAngle apaga motores y deja de ejecutar/entrenar
 *    el controlador; lo reactiva (con estado reiniciado) al volver a |ángulo| < rearmAngle.
 *    Arranca desactivado hasta que el robot se pone vertical.
 *  - Parada de emergencia (setEStop): apaga motores y bloquea el re-armado hasta
 *    setEStop(false).
 *  - No imprime: encola una trama de telemetría por ciclo (receiveTelemetry). Si nadie
 *    la lee a tiempo la trama se descarta; los huecos se ven en `seq`.
 *
 * Pausa: pauseControl() pide a la tarea que se detenga al final de su ciclo (sin
 * suspenderla en medio de una transacción I2C) y espera la confirmación;
 * resumeControl() la reanuda re-sincronizando ángulo y estado.
 */
#pragma once
#include <Arduino.h>

enum class RobotState : uint8_t
{
  Idle,   // motores off, esperando |ángulo| < rearmAngle
  Active, // controlando
  EStop   // parada de emergencia, no se re-arma
};

struct Telemetry
{
  uint32_t seq;   // contador de ciclos de control
  uint32_t tMs;   // millis() al tomar la muestra
  float angle;    // [°]
  float angleRef; // ángulo deseado [°]
  float rate;     // velocidad angular [°/s]
  float pwm;      // salida del controlador
  float pwmMotor; // PWM aplicado tras compensar la zona muerta
  float rpmL;
  float rpmR;
  float kp;
  float dt;       // [s]
  RobotState state;
};

/** Crea la cola de telemetría y TaskBalanceo. Llamar al final de setup(). */
void startControlTask();

/**
 * @brief Detiene el control y los motores y espera a que la tarea confirme.
 * @return true si la tarea quedó en pausa antes de timeoutMs.
 */
bool pauseControl(uint32_t timeoutMs = 500);

/** Reanuda el control (queda desactivado hasta que el robot esté vertical). */
void resumeControl();

/**
 * @brief Pausa el control, ejecuta `action` y lo reanuda.
 * @return false si la tarea no se detuvo (la acción no se ejecuta).
 */
bool runWithControlPaused(void (*action)());

/** Activa (true) o libera (false) la parada de emergencia. */
void setEStop(bool on);

/** @return true si la parada de emergencia está activa. */
bool isEStop();

/** Saca la trama más antigua de la cola, sin bloquear. @return false si está vacía. */
bool receiveTelemetry(Telemetry &out);

/** @return Tramas descartadas porque la cola estaba llena. */
uint32_t droppedTelemetry();

/** @return "IDLE", "ACTIVE" o "ESTOP". */
const char *robotStateName(RobotState s);
