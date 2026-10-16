#include "rpm_cascade.h"
#include "config.h"
#include "encoders.h"
#include "rpm_loop.h"
#include <esp_timer.h>

namespace
{
const uint8_t RING = 16; // cuentas de los últimos 16 ciclos (80 ms a 200 Hz)

RpmLoop rpmLoop;
float rpmRef = 0;
float accel = 0;
int32_t ringC[RING];
int64_t ringT[RING];
uint32_t cycles = 0;
} // namespace

void rpmCascadeReset()
{
  rpmLoop.reset();
  rpmRef = 0;
  accel = 0;
  cycles = 0;
}

float rpmCascadeStep(const Params &p, float angle, float rate, uint32_t periodMs, float dt)
{
  // --- Medición: RPM de la rueda derecha en una ventana de rpm_win ms ---
  int32_t cL, cR;
  encoderCounts(cL, cR);
  const int64_t now = esp_timer_get_time();
  ringC[cycles % RING] = cR;
  ringT[cycles % RING] = now;
  uint32_t back = periodMs ? (p.rpmWinMs + periodMs / 2) / periodMs : 1;
  back = constrain(back, 1u, (uint32_t)RING - 1);
  float meas = 0;
  if (cycles >= back)
  {
    const uint8_t k = (cycles - back) % RING;
    const float dtw = (now - ringT[k]) * 1e-6f;
    if (dtw > 0)
      meas = (cR - ringC[k]) / PPR_WHEEL / dtw * 60.0f;
  }
  cycles++;

  // --- Lazo de ángulo: aceleración de rueda pedida, integrada en la RPM deseada ---
  const float e = angle - p.setpointAngle;
  accel = p.cascadeSign * (p.kTheta * e + p.kOmega * rate) + p.kVel * rpmRef;
  // La saturación detiene la integración (anti-windup del integrador de RPM)
  rpmRef = constrain(rpmRef + accel * dt, -p.rpmRefMax, p.rpmRefMax);

  // --- Lazo de RPM (M2) ---
  const RpmConfig c = {p.rpmKp, p.rpmKi, (uint8_t)periodMs, p.rpmWinMs, true, p.rpmFfK, p.rpmFfB,
                       RPM_I_MAX_DEFAULT, true, p.rpmRefTauMs, RPM_FF_BLEND, false};
  // rpmRef exactamente 0 sólo al arrancar: el lazo devuelve 0 (freno) en ese ciclo
  return rpmLoop.step(c, rpmRef, meas, dt);
}

CascadeState rpmCascadeState()
{
  return {accel, rpmRef, rpmLoop.meas, rpmLoop.uF, rpmLoop.uP, rpmLoop.integ};
}
