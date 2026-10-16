/**
 * @file bench.h
 * @brief Banco de motores: pruebas de lazo abierto (M0) y lazo de RPM (M2) de cada motor.
 *
 * Con el robot sobre un soporte (ruedas en el aire), el banco toma los motores y deja el
 * control de equilibrio en pausa mientras está activo.
 *
 * - TaskBanco (núcleo 1, prioridad 4) corre cada 1 ms: lee los encoders, calcula el mando y
 *   aplica el PWM; cada BENCH_LIVE_MS encola una trama "mot".
 * - Modo PWM: valor fijo por motor, sin compensar zona muerta.
 * - Modo RPM (M2): lazo de la rueda derecha cada `ts_ms`, con la RPM medida sobre los últimos
 *   `win_ms`: u = u_ff + Kp·e + Ki·∫e (anti-windup por integración condicional y tope). El
 *   izquierdo, con el encoder dañado, recibe el mismo PWM (seguidor) si `follow`. Con
 *   referencia 0 el PWM es 0 (freno) y el integral se reinicia.
 * - El mando manual (PWM o RPM) se apaga si no se renueva en BENCH_MANUAL_TIMEOUT_MS.
 * - Ensayo: tramos [valor, ms] de PWM o de RPM (`kind`), muestreados cada `period_ms` (1..20)
 *   en RAM. En la muestra k se leen las cuentas c[k] y se aplica u[k], que queda hasta k+1.
 *   Cada muestra guarda [t_us, pwmL, pwmR, cL, cR, ref·10]. La última deja el mando en 0.
 * - Parada de emergencia: mando a 0, motores en standby y ensayo abortado. Al salir del banco
 *   se activa la parada, para que el control no arranque con el robot sobre el soporte.
 * - PWM 0 con el driver habilitado es freno en corto del TB6612FNG.
 */
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

/** Configuración del lazo de RPM. */
struct RpmConfig
{
  float kp;      // [PWM/RPM]
  float ki;      // [PWM/(RPM·s)]
  uint8_t tsMs;  // período del lazo
  uint8_t winMs; // ventana de medición
  bool ff;       // prealimentación activa
  float ffK;     // [RPM/PWM]
  float ffB;     // [RPM]
  float iMax;    // tope del integral [PWM]
  bool follow;   // el izquierdo recibe el mismo PWM
};

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
  bool rpmMode;        // el mando viene del lazo de RPM
  float ref;           // referencia de RPM
  float meas;          // RPM que mide el lazo (ventana win_ms)
  float uF, uP, uI;    // términos del mando [PWM]
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

/** Modo PWM: PWM manual de cada motor, en [-255, 255]. */
bool benchSetPwm(int pwmL, int pwmR, String &err);

/** Modo RPM: referencia de la rueda derecha, en [-BENCH_MAX_RPM, BENCH_MAX_RPM]. */
bool benchSetRpm(float ref, String &err);

/** Cambia la configuración del lazo de RPM (valida rangos; todo o nada). */
bool benchSetRpmConfig(const RpmConfig &cfg, String &err);

/** Configuración activa del lazo de RPM. */
RpmConfig benchRpmConfig();

/**
 * @brief Inicia un ensayo.
 * @param motorMask bit0 = izquierdo, bit1 = derecho (en un ensayo de RPM: el lazo es del
 *                  derecho; el izquierdo sigue según `follow`).
 * @param rpmKind   true: los valores de los tramos son referencias de RPM; false: PWM.
 */
bool benchRun(uint8_t motorMask, bool rpmKind, const float *val, const uint32_t *ms, uint8_t n, uint32_t periodMs,
              String &err);

/** Mando manual a 0 y ensayo en curso abortado. */
void benchStop();

/** @return Muestras legibles del último ensayo (0 si no hay o sigue en curso). */
uint32_t benchReadableSamples();

/** Escribe las filas [t_us, pwmL, pwmR, cL, cR, ref·10] desde `from`, separadas por comas. */
void benchWriteRows(Print &out, uint32_t from, uint32_t count);

/** Estado del banco, del lazo de RPM y del último ensayo. */
void benchStatusJson(JsonObject out);

/** Saca la trama más antigua, sin bloquear. @return false si está vacía. */
bool receiveBenchFrame(BenchFrame &out);

/** Mensajes para humanos del banco (fin de ensayo, mando vencido). Llamar desde loop(). */
void benchLoop();

/** @return "none", "running", "done" o "aborted". */
const char *benchCapStateName(uint8_t s);
