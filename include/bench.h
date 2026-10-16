/**
 * @file bench.h
 * @brief Banco de motores: pruebas de lazo abierto de cada motor (fase M0).
 *
 * Con el robot sobre un soporte (ruedas en el aire), el banco toma los motores y deja el
 * control de equilibrio en pausa mientras está activo.
 *
 * - TaskBanco (núcleo 1, prioridad 4) corre cada 1 ms: lee los encoders, aplica el PWM y,
 *   cada BENCH_LIVE_MS, encola una trama "mot" (cuentas, RPM, flancos por canal y nivel de
 *   los pines de cada encoder).
 * - PWM manual: valor fijo por motor, sin compensar zona muerta. Se apaga solo si no se
 *   renueva en BENCH_MANUAL_TIMEOUT_MS (la HMI lo renueva mientras esté distinto de 0).
 * - Ensayo: una secuencia de tramos [PWM, ms] sobre el motor L, R o ambos, muestreada cada
 *   `period_ms` (1..20 ms) en RAM. En la muestra k se leen las cuentas c[k] y justo después
 *   se aplica u[k], que queda hasta la muestra k+1. La última muestra deja el PWM en 0.
 *   Al terminar, el ensayo se lee por partes con el comando "bench" op "get".
 * - Parada de emergencia: PWM a 0, motores en standby y ensayo abortado (lo medido hasta
 *   ahí se puede leer). Al salir del banco se activa la parada, para que el control no
 *   arranque con el robot vertical sobre el soporte.
 * - PWM 0 con el driver habilitado es freno en corto del TB6612FNG (igual que en el
 *   control); en parada de emergencia los motores quedan libres (standby).
 */
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

/** Trama en vivo del banco (mensaje "mot"). */
struct BenchFrame
{
  uint32_t seq;
  uint32_t tMs;        // millis()
  int16_t pwmL, pwmR;  // PWM aplicado (sin zona muerta)
  int32_t cL, cR;      // cuentas acumuladas desde el arranque (con ENC_*_SIGN)
  float rpmL, rpmR;    // RPM de rueda en los últimos BENCH_LIVE_MS
  uint32_t edges[4];   // flancos por canal desde que se entró al banco: L_A, L_B, R_A, R_B
  uint8_t levels;      // bit0 L_A, bit1 L_B, bit2 R_A, bit3 R_B
  bool estop;          // parada de emergencia activa
  uint8_t capState;    // 0 ninguno, 1 en curso, 2 terminado, 3 abortado
  uint16_t capId;
  uint32_t capCount;   // muestras tomadas
  uint32_t capTotal;   // muestras previstas
};

/** Crea la cola y TaskBanco (inactiva). Llamar en setup(). */
void benchInit();

/** @return true si el banco está activo. */
bool benchActive();

/** Pausa el control, reserva la memoria del ensayo y toma los motores. */
bool benchEnter(String &err);

/** Suelta los motores, libera la memoria, activa la parada de emergencia y reanuda el control. */
void benchExit();

/** PWM manual de cada motor, en [-255, 255]. */
bool benchSetPwm(int pwmL, int pwmR, String &err);

/**
 * @brief Inicia un ensayo.
 * @param motorMask bit0 = izquierdo, bit1 = derecho.
 * @param segments  pares (PWM, ms).
 */
bool benchRun(uint8_t motorMask, const int16_t *pwm, const uint32_t *ms, uint8_t n, uint32_t periodMs, String &err);

/** PWM manual a 0 y ensayo en curso abortado. */
void benchStop();

/** @return Muestras legibles del último ensayo (0 si no hay o sigue en curso). */
uint32_t benchReadableSamples();

/** Escribe las filas [t_us, pwmL, pwmR, cL, cR] desde `from`, separadas por comas. */
void benchWriteRows(Print &out, uint32_t from, uint32_t count);

/** Estado del banco y del último ensayo. */
void benchStatusJson(JsonObject out);

/** Saca la trama más antigua, sin bloquear. @return false si está vacía. */
bool receiveBenchFrame(BenchFrame &out);

/** Mensajes para humanos del banco (fin de ensayo, PWM vencido). Llamar desde loop(). */
void benchLoop();

/** @return "none", "running", "done" o "aborted". */
const char *benchCapStateName(uint8_t s);
