/**
 * @file mpu_block.h
 * @brief MPU6050: inicialización, calibración persistente (NVS) y ángulo filtrado.
 *
 * - Escalas: acelerómetro 16384 LSB/g (±2 g), giroscopio 131 LSB/(°/s) (±250 °/s).
 * - Ángulo de inclinación: atan2(ay, az) corregido con gx mediante filtro
 *   complementario 0.98 (giroscopio) / 0.02 (acelerómetro).
 * - Offsets guardados en Preferences, namespace "mpu", claves
 *   "ax_off", "ay_off", "az_off", "gx_off".
 * - Todas las funciones usan el bus I2C: llamarlas sólo desde un contexto a la
 *   vez (setup(), TaskBalanceo, o loop() con el control en pausa).
 */
#pragma once
#include <Arduino.h>

/**
 * @brief Inicia I2C y el MPU6050.
 * @return true si el sensor responde (testConnection).
 */
bool setupMPU();

/** @return true si había offsets guardados en NVS y se cargaron. */
bool loadCalibration();

/**
 * @brief Promedia 500 muestras con el robot quieto y en vertical, calcula los
 *        offsets y los guarda en NVS. Bloqueante (~1.5 s).
 */
void calibrateMPU();

/** Inicializa el ángulo filtrado sólo con el acelerómetro (tras arrancar o calibrar). */
void resetAngleFromAccel();

/**
 * @brief Lee el MPU6050 y actualiza el filtro complementario.
 * @param dt Tiempo desde la última llamada [s].
 * @return Ángulo filtrado [°].
 */
float updateAngle(float dt);

/** @return Velocidad angular del eje de inclinación de la última lectura [°/s]. */
float getAngularRate();
