/**
 * @file console.h
 * @brief Salida de texto para humanos (logs, ayuda, resultados de pruebas).
 *
 * Escribe en el Serial y en las salidas registradas (WebSocket, MQTT), línea a línea.
 * El gateway trata toda línea que no empiece con '{' como log del robot, así que los
 * mensajes llegan igual por USB o por WiFi. Usar sólo desde loop() (un solo hilo).
 */
#pragma once
#include <Arduino.h>

extern Print &Console;

/** Registra otra salida (WebSocket, MQTT) y cómo saber si llegó algo por ella. Hasta 3. */
void consoleAddMirror(Print *mirror, bool (*inputPending)());

/**
 * @return true si llegó algo por el Serial (se descarta) o por otra salida (queda en
 *         cola). La prueba de zona muerta lo usa para abortar.
 */
bool consoleAbortRequested();
