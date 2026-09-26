/**
 * @file motor_test.h
 * @brief Prueba de zona muerta de los motores (PWM mínimo que vence la fricción).
 *
 * Se lanza desde el monitor serie con el comando 'd' (ver main.cpp). Con el control
 * en pausa, sube el PWM de ambos motores desde 0 en pasos de 1 hasta que el encoder
 * derecho detecta giro sostenido (DEADBAND_TEST_CONFIRM_STEPS escalones seguidos con al
 * menos DEADBAND_TEST_PULSES en el sentido pedido), primero hacia adelante y luego hacia
 * atrás, e imprime el resultado. Así se ignora el juego de la reductora, que el encoder
 * (montado en el eje del motor) registra antes de que gire la rueda. Cualquier tecla la aborta y detiene los motores.
 *
 * Sólo se mide la rueda derecha (el encoder izquierdo está defectuoso); observar a
 * simple vista si la rueda izquierda arranca a la vez.
 *
 * Requisito: TaskBalanceo debe estar en pausa (pauseControl) antes de llamarla.
 */
#pragma once
#include <Arduino.h>

/** Ejecuta la prueba (bloqueante, ~20 s como máximo) e imprime los resultados. */
void runDeadbandTest();
