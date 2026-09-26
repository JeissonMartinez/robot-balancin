/**
 * @file protocol.h
 * @brief Protocolo de comunicación con la HMI (docs/hmi/PROTOCOLO.md).
 *
 * Independiente del transporte: recibe una línea JSON ya separada y escribe las
 * respuestas en un Print (hoy Serial; en F4 también el WebSocket).
 *
 * - Comandos: {"type":"cmd","id":N,"cmd":"...", ...} → una respuesta "ack" con el mismo id.
 * - Telemetría: {"type":"tel", ...} una por ciclo de control, decimada con `div`.
 * - Todo lo que no empiece con '{' en la salida es texto libre (logs para humanos).
 */
#pragma once
#include <Arduino.h>
#include "tasks_block.h"

struct TelemetryOutput
{
  bool enabled;
  bool json;   // false = línea de texto legible
  uint8_t div; // se envía 1 de cada `div` tramas (1 = 50 Hz)
};

/** Configuración de la salida de telemetría (compartida con los comandos de teclado). */
TelemetryOutput &telemetryOutput();

/** Escribe el mensaje "hello" (versión de firmware y protocolo). */
void protocolHello(Print &out);

/** Interpreta una línea JSON y escribe su respuesta. Puede bloquear (calib, deadband, save). */
void protocolHandleLine(const char *line, Print &out);

/** Escribe una trama de telemetría en JSON o en texto. */
void protocolWriteTelemetry(const Telemetry &t, Print &out, bool json);

/** Escribe los parámetros activos (mensaje "params"). */
void protocolWriteParams(Print &out);

/** Calibración del MPU con el control en pausa. @return false si no se pudo pausar. */
bool runCalibration();

/** Prueba de zona muerta con el control en pausa. @return false si no se pudo pausar. */
bool runDeadband();
