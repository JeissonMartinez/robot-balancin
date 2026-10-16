/**
 * @file rpm_loop.h
 * @brief Lazo de RPM de una rueda (M2): prealimentación + PI con modelo de referencia.
 *
 * u = u_ff(r) + Kp·e + Ki·∫e,  e = r_f − y
 *  - u_ff = signo(r)·(|r| + B·s)/K con s = min(1, |r|/ffBlend): inversa de la recta estática
 *    del motor (M0). Con ffBlend > 0 el desplazamiento B crece desde 0 cerca de r = 0, para que
 *    una referencia que oscila alrededor de 0 (equilibrio, M3) no dé golpes de ±B/K.
 *  - r_f: referencia filtrada con la τ del motor (modelo de referencia): si el motor se parece
 *    al modelo, el error en las subidas es ~0 y el PI no agrega sobrepico.
 *  - Integración condicional (no integra si el mando está saturado en el sentido del error)
 *    y tope ±iMax.
 *  - Referencia exactamente 0: PWM 0 (freno) y estado a cero.
 *  - resetOnSignChange: el integral se descarta al cambiar el sentido (banco); en el
 *    equilibrio la referencia cruza 0 todo el tiempo y no conviene.
 *
 * Lo usan el banco de motores (bench.cpp) y la estructura RpmCascade (rpm_cascade.cpp).
 */
#pragma once
#include <Arduino.h>

/** Configuración del lazo de RPM. */
struct RpmConfig
{
  float kp;         // [PWM/RPM]
  float ki;         // [PWM/(RPM·s)]
  uint8_t tsMs;     // período del lazo
  uint8_t winMs;    // ventana de medición
  bool ff;          // prealimentación activa
  float ffK;        // [RPM/PWM]
  float ffB;        // [RPM]
  float iMax;       // tope del integral [PWM]
  bool follow;      // el izquierdo recibe el mismo PWM
  uint8_t refTauMs; // modelo de referencia: τ del filtro de la referencia (0 = sin filtro)
  float ffBlend;    // [RPM] el desplazamiento B crece linealmente hasta esta |r| (0 = escalón)
  bool resetOnSignChange;
};

/** Estado del lazo. */
struct RpmLoop
{
  float integ = 0, uF = 0, uP = 0, out = 0, meas = 0, lastRef = 0, refF = 0;

  void reset();

  /** Un paso del lazo. `y`: RPM medida; `dt` en s. @return PWM a aplicar (±255). */
  float step(const RpmConfig &c, float ref, float y, float dt);
};
