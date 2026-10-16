/**
 * @file rpm_cascade.h
 * @brief Estructura RpmCascade: lazo de ángulo (M3), posición y velocidad (M4) y rumbo (M4b)
 *        sobre el lazo de RPM de M2.
 *
 *   α = s·[Kθ·(θ − θ0) + Kω·ω] + Kv·(RPM_ref − v_ref) + Kx·(x − x_ref)      [RPM/s]
 *   RPM_ref ← RPM_ref + α·dt (±rpm_ref_max);  x_ref ← x_ref + v_ref·dt
 *   PWM = lazo de RPM de la rueda derecha (rpm_loop.h)
 *   Rumbo: ψ = ∫ω_z, ψ_ref = ∫r_ref;  Δ = s_ψ·[Kψ·(ψ_ref − ψ) + Kr·(r_ref − ω_z)]
 *   PWM_izq = PWM + Δ   (el izquierdo va en lazo abierto: el giroscopio corrige su desfase)
 *
 * - El lazo de ángulo pide ACELERACIÓN: con la rueda como fuente de velocidad, un PD de ángulo
 *   → velocidad no puede equilibrar (docs/control/M3_LAZO_ANGULO.md).
 * - Kv y Kx van en el mismo sentido que la velocidad y la posición: para frenar o volver, el
 *   robot primero tiene que inclinarse hacia atrás (docs/control/M4_POSICION_VELOCIDAD.md).
 * - Conducción (comando "drive"): v (RPM) y r (°/s) pedidos, con rampa; se vencen a los
 *   DRIVE_TIMEOUT_MS sin renovar. «Volver al inicio» lleva x_ref a 0 con velocidad limitada.
 * - x, x_ref, ψ y ψ_ref se reinician al re-armar el control.
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
  float xCm, xRefCm; // posición y referencia [cm]
  float vRef;    // velocidad pedida (con rampa) [RPM]
  float psi, psiRef; // rumbo y referencia [°]
  float yawRate; // ω_z [°/s]
  float dL;      // corrección del izquierdo [PWM]
};

/** PWM de cada rueda. */
struct CascadeOutput
{
  float pwmL, pwmR;
};

/** Reinicia integradores, posición, rumbo, lazo de RPM y ventana de medición. */
void rpmCascadeReset();

/**
 * @brief Un paso de la estructura RpmCascade.
 * @param angle Ángulo [°]; @param rate Velocidad angular de inclinación [°/s];
 * @param yawRate Velocidad de giro vertical [°/s]; @param periodMs Periodo del control;
 * @param dt Tiempo medido [s].
 */
CascadeOutput rpmCascadeStep(const Params &p, float angle, float rate, float yawRate, uint32_t periodMs, float dt);

/** Estado del último paso. */
CascadeState rpmCascadeState();

/** Conducción: velocidad [RPM] y giro [°/s] pedidos; cancela «volver al inicio». Llamar desde loop(). */
void driveCommand(float vRpm, float rDps);

/** «Volver al inicio»: lleva x_ref a 0 con velocidad limitada. */
void driveHome();

/** Estado de la conducción para el ack: v y r pedidos, si va al inicio. */
void driveStatus(float &v, float &r, bool &home);
