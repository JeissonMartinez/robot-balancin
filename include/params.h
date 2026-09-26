/**
 * @file params.h
 * @brief Parámetros de ajuste del controlador, modificables en ejecución.
 *
 * - Un único juego activo en RAM, protegido con spinlock. TaskBalanceo toma una copia
 *   completa al inicio de cada ciclo (paramsSnapshot), así nunca usa un juego a medias.
 * - Cada cambio valida tipo, rango y reglas cruzadas (KP_MIN ≤ KP_MAX, REARM < MAX)
 *   sobre una copia; si algo falla no se aplica nada.
 * - Persistencia en NVS (namespace "params"). Al arrancar se cargan si existen y son
 *   válidos; si no, se usan los valores de fábrica de config.h.
 * - La tabla de descriptores (params.cpp) es la fuente única de nombres, rangos y
 *   unidades: el comando "schema" la envía a la HMI para construir los controles.
 */
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct Params
{
  ControlStructure structure;
  float setpointAngle;   // [°]
  float kpMin, kpMax;    // Rango de Kp_angle que elige la RN
  float kdAngle;
  float kiAngle;         // AngleOuter
  float kpSpeed, kiSpeed; // AngleOuter
  float kpV, kiV;        // SpeedOuter
  float speedLoopSign;   // ±1
  float speedRefRpm;     // [RPM]
  float maxTiltRefDeg;   // [°]
  float pwmDeadband;
  float nnErrorBand;     // [°]
  float nnLearningRate;
  float kpFilterTau;     // [s]
  float maxAngle;        // [°]
  float rearmAngle;      // [°]
  uint8_t mpuDlpfMode;   // 0..6
  bool useGyroDerivative;
};

/** Carga de NVS (si hay un juego válido) o de fábrica. Llamar una vez en setup(). */
void paramsInit();

/** @return true si los parámetros activos vienen de NVS. */
bool paramsLoadedFromNvs();

/**
 * @brief Copia el juego activo.
 * @return Versión del juego; aumenta con cada cambio aplicado.
 */
uint32_t paramsSnapshot(Params &out);

/** @return Valores de fábrica (config.h). */
Params paramsFactory();

/**
 * @brief Aplica los pares clave-valor de `in` al juego activo, todos o ninguno.
 * @param err Mensaje de error si devuelve false.
 */
bool paramsApplyJson(JsonObjectConst in, String &err);

/** Reemplaza el juego activo por los valores de fábrica (no toca NVS). */
void paramsRestoreFactory();

/**
 * @brief Guarda el juego activo en NVS. Escribe en flash: llamar con el control en
 *        pausa (runWithControlPaused).
 */
bool paramsSave();

/** Escribe todos los parámetros como {clave: valor}. */
void paramsToJson(const Params &p, JsonObject out);

/** Escribe la tabla de descriptores: [{key, type, min, max, def, unit, desc}, ...]. */
void paramsSchemaToJson(JsonArray out);

/** @return Nombre de la estructura ("SpeedOuter" / "AngleOuter"). */
const char *structureName(ControlStructure s);
