/**
 * @file encoders.h
 * @brief Encoders por cuadratura de ambas ruedas y cálculo de RPM.
 *
 * - Interrupción CHANGE sólo en el canal A de cada encoder (x2 respecto a CPR por
 *   canal); el sentido se obtiene comparando A con B.
 * - Las ISR leen los GPIO directamente por registro (IRAM-safe): no llaman a
 *   digitalRead(), que vive en flash y provocaría un fallo si llega un pulso
 *   mientras se escribe en NVS (caché de flash deshabilitada).
 * - Los contadores se protegen con un spinlock, de modo que leer-y-reiniciar es
 *   atómico frente a las ISR y no se pierden pulsos.
 */
#pragma once
#include <Arduino.h>

/** Configura pines e interrupciones de ambos encoders. */
void setupEncoders();

/**
 * @brief Lee y pone a cero los contadores de forma atómica.
 * @param pulsesL Pulsos del encoder izquierdo desde la última llamada.
 * @param pulsesR Pulsos del encoder derecho desde la última llamada.
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
