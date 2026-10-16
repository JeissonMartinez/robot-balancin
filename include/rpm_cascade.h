/**
 * @file rpm_cascade.h
 * @brief Estructura RpmCascade (M3): lazo de ángulo sobre el lazo de RPM de M2.
 *
 *   α = s·[Kθ·(θ − θ_setpoint) + Kω·ω] + Kv·RPM_ref         [RPM/s]
 *   RPM_ref ← RPM_ref + α·dt,   saturada a ±rpm_ref_max (la saturación detiene la integración)
 *   PWM = lazo de RPM (rpm_loop.h) de la rueda derecha con esa referencia; el izquierdo recibe
 *   el mismo PWM (encoder dañado, seguidor).
 *
 * El lazo de ángulo pide ACELERACIÓN de rueda, no velocidad: con la rueda convertida en fuente de
 * velocidad por el lazo de RPM, un PD de ángulo → velocidad no puede equilibrar (falta el término
 * que contrarresta g·θ). Ver docs/control/M3_LAZO_ANGULO.md.
 *
 * La RPM que usa el lazo se mide con una ventana de rpm_win ms sobre las cuentas acumuladas
 * (múltiplo del periodo de control). Uso: rpmCascadeReset() al (re)activar; rpmCascadeStep()
 * cada ciclo de TaskBalanceo.
 */
#pragma once
#include <Arduino.h>
#include "params.h"

/** Valores internos del último paso, para la telemetría. */
struct CascadeState
{
  float accel;   // α pedida [RPM/s]
  float rpmRef;  // RPM deseada (integrador)
  float rpmMeas; // RPM que mide el lazo (ventana)
  float uF, uP, uI; // términos del lazo de RPM [PWM]
};

/** Reinicia el integrador de la RPM deseada, el lazo de RPM y la ventana de medición. */
void rpmCascadeReset();

/**
 * @brief Un paso de la estructura RpmCascade.
 * @param angle Ángulo [°]; @param rate Velocidad angular del giroscopio [°/s].
 * @param periodMs Periodo del control (para la ventana); @param dt Tiempo medido [s].
 * @return PWM a aplicar a los dos motores (±255).
 */
float rpmCascadeStep(const Params &p, float angle, float rate, uint32_t periodMs, float dt);

/** Estado del último paso. */
CascadeState rpmCascadeState();
