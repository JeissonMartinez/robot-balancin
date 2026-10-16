#include "rpm_loop.h"
#include "config.h"

static inline float sgn(float v)
{
  return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f);
}

void RpmLoop::reset()
{
  integ = uF = uP = out = 0;
  lastRef = refF = 0;
}

float RpmLoop::step(const RpmConfig &c, float ref, float y, float dt)
{
  meas = y;
  if (ref == 0)
  {
    reset();
    meas = y;
    return 0; // referencia 0: freno, sin integral que empuje
  }
  if (c.resetOnSignChange && sgn(ref) != sgn(lastRef))
    integ = 0; // cambio de sentido: el integral acumulado ya no sirve
  lastRef = ref;
  // Modelo de referencia: la respuesta que se espera del motor a la prealimentación
  if (c.refTauMs > 0)
    refF += (ref - refF) * (1.0f - expf(-dt / (c.refTauMs * 1e-3f)));
  else
    refF = ref;
  const float e = refF - y;
  if (c.ff && c.ffK > 0)
  {
    const float blend = c.ffBlend > 0 ? fminf(1.0f, fabsf(ref) / c.ffBlend) : 1.0f;
    uF = sgn(ref) * (fabsf(ref) + c.ffB * blend) / c.ffK;
  }
  else
    uF = 0;
  uP = c.kp * e;
  // Integración condicional: no integrar si el mando ya está saturado en el sentido del error
  const float candidate = constrain(integ + c.ki * e * dt, -c.iMax, c.iMax);
  const float raw = uF + uP + candidate;
  if (!((raw > PWM_LIMIT && e > 0) || (raw < -PWM_LIMIT && e < 0)))
    integ = candidate;
  out = constrain(uF + uP + integ, -PWM_LIMIT, PWM_LIMIT);
  return out;
}
