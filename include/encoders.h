/**
 * @file encoders.h
 * @brief Encoders por cuadratura de ambas ruedas: cuentas, RPM y diagnóstico.
 *
 * - Cada encoder usa una unidad del contador de pulsos por hardware (PCNT) en cuadratura
 *   x4: cuenta los flancos de subida y de bajada de A y de B, sin interrupciones por
 *   pulso. Un filtro de glitches (ENC_GLITCH_NS) descarta los pulsos de ruido cortos.
 * - El contador de 16 bits se lee cada 10 ms (esp_timer) y en cada consulta, y se acumula
 *   en 32 bits con aritmética módulo su límite: no se pierde ninguna cuenta.
 * - Convención de signo igual a la de la versión anterior (interrupción en A): ENC_*_SIGN
 *   sigue haciendo que PWM > 0 dé cuentas y RPM > 0.
 * - Diagnóstico (banco de motores): contadores de flancos de cada canal por separado,
 *   con interrupciones que sólo se activan en el banco, y nivel actual de cada pin.
 *   Si un canal no tiene flancos, el PCNT no puede contar (cuadratura sin una fase).
 * - ENC_L_SINGLE_CHANNEL: el izquierdo cuenta sólo los flancos de A (x2, cada uno vale 2
 *   cuentas) con el sentido del último PWM aplicado a ese motor (motorDirectionL()).
 */
#pragma once
#include <Arduino.h>

/** Configura pines y contadores de ambos encoders. */
void setupEncoders();

/**
 * @brief Pulsos de cada encoder desde la última llamada (sin aplicar ENC_*_SIGN).
 *        Lo usan TaskBalanceo y la prueba de zona muerta (nunca a la vez).
 */
void readAndResetEncoders(long &pulsesL, long &pulsesR);

/**
 * @brief Calcula las RPM de cada rueda con filtro IIR (0.7 anterior / 0.3 actual).
 * @param dt Tiempo desde la última llamada [s].
 * @param rpmL_f RPM filtrada rueda izquierda (salida).
 * @param rpmR_f RPM filtrada rueda derecha (salida).
 */
void updateWheelRPM(float dt, float &rpmL_f, float &rpmR_f);

/** Descarta los pulsos acumulados y reinicia el filtro de RPM. */
void resetWheelRPM();

/** Cuentas acumuladas desde el arranque, con ENC_*_SIGN aplicado (PWM > 0 → aumentan). */
void encoderCounts(int32_t &countL, int32_t &countR);

/** Activa o desactiva los contadores de flancos por canal (los pone a cero al activar). */
void encoderEdgeCount(bool on);

/** Flancos por canal desde que se activaron: [L_A, L_B, R_A, R_B]. */
void encoderEdges(uint32_t out[4]);

/** Nivel actual de los pines: bit0 L_A, bit1 L_B, bit2 R_A, bit3 R_B. */
uint8_t encoderLevels();
