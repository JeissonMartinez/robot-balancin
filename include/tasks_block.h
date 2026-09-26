/**
 * @file tasks_block.h
 * @brief Tarea FreeRTOS de balanceo, pausa segura para calibrar y telemetría.
 *
 * TaskBalanceo (núcleo 1, prioridad 3, periodo CONTROL_PERIOD_MS):
 *  - Periodo fijo con vTaskDelayUntil y dt medido con esp_timer_get_time().
 *  - Ángulo (MPU) + RPM (encoders) → cascada() → mismo PWM a ambos motores.
 *  - Seguridad: si |ángulo| > MAX_ANGLE apaga motores y deja de ejecutar/entrenar
 *    el controlador; lo reactiva (con estado reiniciado) al volver a |ángulo| < REARM_ANGLE.
 *    Arranca desactivado hasta que el robot se pone vertical.
 *  - No imprime por Serial: publica una instantánea que loop() imprime.
 *
 * Pausa para calibrar: pauseControl() pide a la tarea que se detenga al final de
 * su ciclo (sin suspenderla en medio de una transacción I2C) y espera la
 * confirmación; resumeControl() la reanuda re-sincronizando ángulo y estado.
 */
#pragma once
#include <Arduino.h>

struct Telemetry
{
  float angle;
  float angleRef; // ángulo deseado [°]
  float rate;     // velocidad angular [°/s]
  float pwm;
  float rpmL;
  float rpmR;
  float kp;
  float dt;
  bool active; // false = motores apagados (caído o esperando vertical)
};

/** Crea TaskBalanceo. Llamar al final de setup(). */
void startControlTask();

/**
 * @brief Detiene el control y los motores y espera a que la tarea confirme.
 * @return true si la tarea quedó en pausa antes de timeoutMs.
 */
bool pauseControl(uint32_t timeoutMs = 500);

/** Reanuda el control (queda desactivado hasta que el robot esté vertical). */
void resumeControl();

/** Copia la última instantánea publicada por TaskBalanceo. */
void getTelemetry(Telemetry &out);
