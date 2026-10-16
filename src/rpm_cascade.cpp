#include "rpm_cascade.h"
#include "config.h"
#include "encoders.h"
#include "rpm_loop.h"
#include <esp_timer.h>

namespace
{
const uint8_t RING = 16; // cuentas de los últimos 16 ciclos (80 ms a 200 Hz)
const float CM_PER_TURN = 2.0f * PI * WHEEL_RADIUS_M * 100.0f;

RpmLoop rpmLoop;
float rpmRef = 0;
float accel = 0;
int32_t ringC[RING];
int64_t ringT[RING];
uint32_t cycles = 0;
bool haveOrigin = false;
int32_t c0 = 0;               // cuentas al re-armar (origen de x)
float x = 0, xRef = 0, vRef = 0; // vueltas, vueltas, RPM
float psi = 0, psiRef = 0, rRef = 0, yaw = 0, dL = 0;

// --- Mando de conducción: lo escribe loop(), lo lee TaskBalanceo ---
portMUX_TYPE driveMux = portMUX_INITIALIZER_UNLOCKED;
float vCmd = 0, rCmd = 0;
uint32_t cmdMs = 0;
bool homing = false;

float approach(float cur, float target, float step)
{
  if (target > cur + step)
    return cur + step;
  if (target < cur - step)
    return cur - step;
  return target;
}
} // namespace

void rpmCascadeReset()
{
  rpmLoop.reset();
  rpmRef = accel = 0;
  cycles = 0;
  haveOrigin = false;
  x = xRef = vRef = 0;
  psi = psiRef = rRef = dL = 0;
  portENTER_CRITICAL(&driveMux);
  vCmd = rCmd = 0;
  homing = false;
  portEXIT_CRITICAL(&driveMux);
}

void driveCommand(float vRpm, float rDps)
{
  portENTER_CRITICAL(&driveMux);
  vCmd = vRpm;
  rCmd = rDps;
  homing = false;
  cmdMs = millis();
  portEXIT_CRITICAL(&driveMux);
}

void driveHome()
{
  portENTER_CRITICAL(&driveMux);
  vCmd = rCmd = 0;
  homing = true;
  portEXIT_CRITICAL(&driveMux);
}

void driveStatus(float &v, float &r, bool &home)
{
  portENTER_CRITICAL(&driveMux);
  v = vCmd;
  r = rCmd;
  home = homing;
  portEXIT_CRITICAL(&driveMux);
}

CascadeOutput rpmCascadeStep(const Params &p, float angle, float rate, float yawRate, uint32_t periodMs, float dt)
{
  // --- Medición: cuentas acumuladas de la rueda derecha ---
  int32_t cL, cR;
  encoderCounts(cL, cR);
  const int64_t now = esp_timer_get_time();
  if (!haveOrigin)
  {
    c0 = cR;
    haveOrigin = true;
  }
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
  x = (cR - c0) / PPR_WHEEL;

  // --- Conducción: velocidad y giro pedidos, con vencimiento y rampa ---
  float vc, rc;
  bool home;
  portENTER_CRITICAL(&driveMux);
  if (!homing && (vCmd != 0 || rCmd != 0) && millis() - cmdMs > DRIVE_TIMEOUT_MS)
    vCmd = rCmd = 0; // el enlace se cortó o se soltó el botón: detenerse donde está
  if (homing && fabsf(xRef) < 0.005f && fabsf(vRef) < 0.5f)
    homing = false;
  vc = vCmd;
  rc = rCmd;
  home = homing;
  portEXIT_CRITICAL(&driveMux);
  if (home)
    vc = constrain(-HOME_GAIN * xRef * 60.0f, -p.vRefMax / 2, p.vRefMax / 2);
  vc = constrain(vc, -p.vRefMax, p.vRefMax);
  rc = constrain(rc, -p.rRefMax, p.rRefMax);
  vRef = approach(vRef, vc, p.vAccel * dt);
  rRef = approach(rRef, rc, 2.0f * p.rRefMax * dt); // de 0 al máximo en 0.5 s
  xRef += vRef / 60.0f * dt;

  // --- Lazo de ángulo con posición y velocidad: aceleración de rueda → RPM deseada ---
  const float e = angle - p.setpointAngle;
  accel = p.cascadeSign * (p.kTheta * e + p.kOmega * rate) + p.kVel * (rpmRef - vRef) + p.kX * (x - xRef);
  rpmRef = constrain(rpmRef + accel * dt, -p.rpmRefMax, p.rpmRefMax);

  // --- Lazo de RPM (M2) ---
  const RpmConfig c = {p.rpmKp, p.rpmKi, (uint8_t)periodMs, p.rpmWinMs, true, p.rpmFfK, p.rpmFfB,
                       RPM_I_MAX_DEFAULT, true, p.rpmRefTauMs, RPM_FF_BLEND, false};
  const float u = rpmLoop.step(c, rpmRef, meas, dt);

  // --- Rumbo (M4b): corrección del izquierdo con el giroscopio z ---
  yaw = yawRate;
  psi += yawRate * dt;
  psiRef += rRef * dt;
  dL = p.yawSign * (p.kPsi * (psiRef - psi) + p.kR * (rRef - yawRate));
  dL = constrain(dL, -YAW_DELTA_MAX, YAW_DELTA_MAX);
  return {constrain(u + dL, -PWM_LIMIT, PWM_LIMIT), u};
}

CascadeState rpmCascadeState()
{
  return {accel, rpmRef, rpmLoop.meas, rpmLoop.uF, rpmLoop.uP, rpmLoop.integ, x * CM_PER_TURN, xRef * CM_PER_TURN,
          vRef, psi, psiRef, yaw, dL};
}
