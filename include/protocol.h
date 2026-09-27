/**
 * @file protocol.h
 * @brief Protocolo de comunicación con la HMI (docs/hmi/PROTOCOLO.md).
 *
 * Independiente del transporte: recibe una línea JSON ya separada y responde por el
 * canal de donde vino (Serial o WebSocket). Cada canal tiene su propia configuración de
 * telemetría: el gateway por WiFi puede pedir JSON a 50 Hz sin llenar el monitor serie.
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

/** Un canal de comunicación: dónde se escriben las respuestas y cómo quiere la telemetría. */
struct Channel
{
  Print &out;
  TelemetryOutput tel;
};

/** Escribe el mensaje "hello" (versión de firmware y protocolo). */
void protocolHello(Print &out);

/** Interpreta una línea JSON y responde por el canal. Puede bloquear (calib, deadband, save). */
void protocolHandleLine(const char *line, Channel &ch);

/** Escribe una trama de telemetría en JSON o en texto. */
void protocolWriteTelemetry(const Telemetry &t, Print &out, bool json);

/** Escribe los parámetros activos (mensaje "params"). */
void protocolWriteParams(Print &out);

/** Calibración del MPU con el control en pausa. @return false si no se pudo pausar. */
bool runCalibration();

/** Prueba de zona muerta con el control en pausa. @return false si no se pudo pausar. */
bool runDeadband();
