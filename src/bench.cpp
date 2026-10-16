#include "bench.h"
#include "config.h"
#include "console.h"
#include "encoders.h"
#include "motors.h"
#include "tasks_block.h"
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace
{
struct Sample
{
  uint32_t tUs;
  int16_t pwmL, pwmR;
  int32_t cL, cR;
  int16_t refX10; // referencia de RPM · 10 (0 en ensayos de PWM)
};

enum CapState : uint8_t
{
  CAP_NONE = 0,
  CAP_RUNNING = 1,
  CAP_DONE = 2,
  CAP_ABORTED = 3
};

const uint8_t RING = 64; // cuentas del último 64 ms, una por tick, para la ventana del lazo

TaskHandle_t task = nullptr;
QueueHandle_t frameQueue = nullptr;
const UBaseType_t FRAME_QUEUE_LEN = 25; // 0.5 s a 50 Hz
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> active(false);
std::atomic<bool> taskIdle(true);

// --- Mando: lo escribe loop() y lo lee la tarea, siempre con `mux` ---
bool manualRpm = false; // modo del mando manual
int16_t manualL = 0, manualR = 0;
float manualRef = 0;
uint32_t manualMs = 0;
bool manualExpired = false;
RpmConfig cfg = {RPM_KP_DEFAULT, RPM_KI_DEFAULT, RPM_TS_MS_DEFAULT, RPM_WIN_MS_DEFAULT, true,
                 RPM_FF_K_DEFAULT, RPM_FF_B_DEFAULT, RPM_I_MAX_DEFAULT, true, RPM_REF_TAU_MS_DEFAULT};

Sample *buf = nullptr;
float segVal[BENCH_MAX_SEGMENTS];
uint32_t segMs[BENCH_MAX_SEGMENTS];
uint8_t nSegs = 0;
bool capRpm = false;
uint8_t capMotor = 0;
uint32_t capPeriod = 2;
uint32_t capTotal = 0;
uint32_t capCount = 0;
uint8_t capState = CAP_NONE;
uint16_t capId = 0;
bool capStartPending = false;

bool fail(String &err, const String &msg)
{
  err = msg;
  return false;
}

// Valor del tramo que contiene el instante t [ms] del ensayo; 0 después del último
float segmentAt(uint32_t tMs)
{
  uint32_t end = 0;
  for (uint8_t i = 0; i < nSegs; i++)
  {
    end += segMs[i];
    if (tMs < end)
      return segVal[i];
  }
  return 0;
}

inline float sgn(float v)
{
  return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f);
}

/** Estado del lazo de RPM (sólo lo usa TaskBanco). */
struct RpmLoop
{
  float integ = 0, uF = 0, uP = 0, out = 0, meas = 0, lastRef = 0, refF = 0;

  void reset()
  {
    integ = uF = uP = out = 0;
    lastRef = refF = 0;
  }

  /** Un paso del lazo. `dt` en s; devuelve el PWM a aplicar. */
  float step(const RpmConfig &c, float ref, float y, float dt)
  {
    meas = y;
    if (ref == 0)
    {
      reset();
      meas = y;
      return 0; // referencia 0: freno, sin integral que empuje
    }
    if (sgn(ref) != sgn(lastRef))
      integ = 0; // cambio de sentido: el integral acumulado ya no sirve
    lastRef = ref;
    // Modelo de referencia: la respuesta que se espera del motor a la prealimentación
    if (c.refTauMs > 0)
      refF += (ref - refF) * (1.0f - expf(-dt / (c.refTauMs * 1e-3f)));
    else
      refF = ref;
    const float e = refF - y;
    uF = c.ff && c.ffK > 0 ? sgn(ref) * (fabsf(ref) + c.ffB) / c.ffK : 0;
    uP = c.kp * e;
    // Integración condicional: no integrar si el mando ya está saturado en el sentido del error
    const float candidate = constrain(integ + c.ki * e * dt, -c.iMax, c.iMax);
    const float raw = uF + uP + candidate;
    if (!((raw > PWM_LIMIT && e > 0) || (raw < -PWM_LIMIT && e < 0)))
      integ = candidate;
    out = constrain(uF + uP + integ, -PWM_LIMIT, PWM_LIMIT);
    return out;
  }
};

void TaskBanco(void *)
{
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t tick = 0, seq = 0;
  bool driverOn = false;
  int16_t appliedL = 0, appliedR = 0;
  bool forceWrite = true;
  int32_t liveL = 0, liveR = 0;
  int64_t liveUs = 0;
  uint32_t capTick0 = 0;
  int32_t capL0 = 0, capR0 = 0;
  int64_t capUs0 = 0;
  int16_t capUL = 0, capUR = 0;
  float capRef = 0;
  int32_t ringR[RING];
  int64_t ringT[RING];
  RpmLoop loop;
  int16_t rpmU = 0; // PWM del lazo, se mantiene entre pasos
  bool wasRpm = false;

  while (true)
  {
    if (!active.load())
    {
      if (driverOn)
      {
        stopMotors();
        driverOn = false;
      }
      taskIdle.store(true);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      taskIdle.store(false);
      lastWake = xTaskGetTickCount();
      encoderCounts(liveL, liveR);
      liveUs = esp_timer_get_time();
      tick = 0;
      forceWrite = true;
      loop.reset();
      rpmU = 0;
      wasRpm = false;
      continue;
    }

    vTaskDelayUntil(&lastWake, 1);
    tick++;
    const int64_t nowUs = esp_timer_get_time();
    int32_t cL, cR;
    encoderCounts(cL, cR);
    ringR[tick % RING] = cR;
    ringT[tick % RING] = nowUs;
    const bool estop = isEStop();

    int16_t uL = 0, uR = 0;
    bool rpmActive = false;
    float ref = 0;
    bool takeSample = false;
    uint32_t k = 0;

    portENTER_CRITICAL(&mux);
    const RpmConfig c = cfg;
    if (estop)
    {
      manualL = manualR = 0;
      manualRef = 0;
      capStartPending = false;
      if (capState == CAP_RUNNING)
        capState = CAP_ABORTED;
    }
    if (capStartPending)
    {
      capStartPending = false;
      capTick0 = tick;
      capL0 = cL;
      capR0 = cR;
      capUs0 = nowUs;
      capCount = 0;
    }
    if (capState == CAP_RUNNING)
    {
      const uint32_t elapsed = tick - capTick0;
      if (elapsed % capPeriod == 0)
      {
        k = elapsed / capPeriod;
        takeSample = true;
        const float v = (k + 1 >= capTotal) ? 0 : segmentAt(k * capPeriod);
        if (capRpm)
          capRef = v;
        else
        {
          capUL = (capMotor & 1) ? (int16_t)v : 0;
          capUR = (capMotor & 2) ? (int16_t)v : 0;
        }
      }
      rpmActive = capRpm;
      ref = capRef;
      uL = capUL;
      uR = capUR;
    }
    else
    {
      if ((manualL || manualR || manualRef != 0) && millis() - manualMs > BENCH_MANUAL_TIMEOUT_MS)
      {
        manualL = manualR = 0;
        manualRef = 0;
        manualExpired = true;
      }
      rpmActive = manualRpm;
      ref = manualRef;
      uL = manualL;
      uR = manualR;
    }
    portEXIT_CRITICAL(&mux);

    // --- Lazo de RPM de la rueda derecha ---
    if (rpmActive)
    {
      if (!wasRpm)
      {
        loop.reset();
        rpmU = 0;
      }
      const uint8_t ts = c.tsMs ? c.tsMs : 1;
      if (tick % ts == 0 && tick > c.winMs)
      {
        const uint8_t back = (tick - c.winMs) % RING;
        const float dtWin = (nowUs - ringT[back]) * 1e-6f;
        const float y = dtWin > 0 ? (cR - ringR[back]) / PPR_WHEEL / dtWin * 60.0f : 0;
        rpmU = (int16_t)lroundf(loop.step(c, ref, y, ts * 1e-3f));
      }
      if (ref == 0)
        rpmU = 0; // una referencia 0 frena enseguida, sin esperar al próximo paso
      uR = rpmU;
      uL = c.follow ? rpmU : 0;
    }
    else if (wasRpm)
      loop.reset();
    wasRpm = rpmActive;

    // --- Muestra del ensayo (cuentas de este tick, mando que se aplica ahora) ---
    if (takeSample)
    {
      portENTER_CRITICAL(&mux);
      if (capState == CAP_RUNNING && buf)
      {
        buf[k] = {(uint32_t)(nowUs - capUs0), uL, uR, cL - capL0, cR - capR0, (int16_t)lroundf(ref * 10)};
        capCount = k + 1;
        if (capCount >= capTotal)
          capState = CAP_DONE;
      }
      portEXIT_CRITICAL(&mux);
    }

    // --- Motores ---
    if (estop)
    {
      if (driverOn)
      {
        stopMotors();
        driverOn = false;
      }
      uL = uR = 0;
    }
    else
    {
      if (!driverOn)
      {
        enableMotors();
        driverOn = true;
        forceWrite = true;
      }
      if (forceWrite || uL != appliedL || uR != appliedR)
      {
        driveMotorsDifferential(uL, uR);
        appliedL = uL;
        appliedR = uR;
        forceWrite = false;
      }
    }

    // --- Trama en vivo ---
    if (tick % BENCH_LIVE_MS == 0)
    {
      BenchFrame f;
      const float dt = (nowUs - liveUs) * 1e-6f;
      f.seq = seq++;
      f.tMs = millis();
      f.pwmL = uL;
      f.pwmR = uR;
      f.cL = cL;
      f.cR = cR;
      f.rpmL = dt > 0 ? (cL - liveL) / PPR_WHEEL / dt * 60.0f : 0;
      f.rpmR = dt > 0 ? (cR - liveR) / PPR_WHEEL / dt * 60.0f : 0;
      encoderEdges(f.edges);
      f.levels = encoderLevels();
      f.estop = estop;
      f.rpmMode = rpmActive;
      f.ref = rpmActive ? ref : 0;
      f.refF = rpmActive ? loop.refF : 0;
      f.meas = loop.meas;
      f.uF = rpmActive ? loop.uF : 0;
      f.uP = rpmActive ? loop.uP : 0;
      f.uI = rpmActive ? loop.integ : 0;
      portENTER_CRITICAL(&mux);
      f.capState = capState;
      f.capId = capId;
      f.capCount = capCount;
      f.capTotal = capTotal;
      portEXIT_CRITICAL(&mux);
      liveL = cL;
      liveR = cR;
      liveUs = nowUs;
      xQueueSend(frameQueue, &f, 0); // si nadie la lee, se descarta
    }
  }
}
} // namespace

void benchInit()
{
  frameQueue = xQueueCreate(FRAME_QUEUE_LEN, sizeof(BenchFrame));
  xTaskCreatePinnedToCore(TaskBanco, "TaskBanco", 6144, NULL, 4, &task, 1);
}

bool benchActive()
{
  return active.load();
}

bool benchEnter(String &err)
{
  if (active.load())
    return true;
  if (!pauseControl())
  {
    resumeControl();
    err = "la tarea de control no se detuvo";
    return false;
  }
  buf = (Sample *)heap_caps_malloc(BENCH_MAX_SAMPLES * sizeof(Sample), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf)
  {
    resumeControl();
    err = "no hay memoria para el ensayo";
    return false;
  }
  portENTER_CRITICAL(&mux);
  manualRpm = false;
  manualL = manualR = 0;
  manualRef = 0;
  manualExpired = false;
  capState = CAP_NONE;
  capStartPending = false;
  capCount = capTotal = 0;
  portEXIT_CRITICAL(&mux);
  encoderEdgeCount(true);
  BenchFrame discard;
  while (xQueueReceive(frameQueue, &discard, 0) == pdTRUE)
    ;
  active.store(true);
  xTaskNotifyGive(task);
  Console.println(">> Banco de motores activo: control en pausa. El robot debe estar sobre un soporte.");
  return true;
}

void benchExit()
{
  if (!active.load())
    return;
  active.store(false);
  for (int i = 0; i < 50 && !taskIdle.load(); i++)
    delay(2);
  stopMotors();
  encoderEdgeCount(false);
  portENTER_CRITICAL(&mux);
  capState = CAP_NONE;
  capCount = capTotal = 0;
  Sample *old = buf;
  buf = nullptr;
  portEXIT_CRITICAL(&mux);
  heap_caps_free(old);
  setEStop(true);
  resumeControl();
  Console.println(">> Banco de motores cerrado. Parada de emergencia activa: Armar para volver al control.");
}

// Comprobaciones comunes a los mandos manuales
static bool canDrive(String &err)
{
  if (!active.load())
    return fail(err, "el banco no está activo");
  if (isEStop())
    return fail(err, "parada de emergencia activa: Armar primero");
  return true;
}

bool benchSetPwm(int pwmL, int pwmR, String &err)
{
  if (!canDrive(err))
    return false;
  if (pwmL < -255 || pwmL > 255 || pwmR < -255 || pwmR > 255)
    return fail(err, "PWM fuera de [-255, 255]");
  bool ok = true;
  portENTER_CRITICAL(&mux);
  if (capState == CAP_RUNNING)
    ok = false;
  else
  {
    manualRpm = false;
    manualRef = 0;
    manualL = pwmL;
    manualR = pwmR;
    manualMs = millis();
  }
  portEXIT_CRITICAL(&mux);
  if (!ok)
    err = "hay un ensayo en curso";
  return ok;
}

bool benchSetRpm(float ref, String &err)
{
  if (!canDrive(err))
    return false;
  if (!(fabsf(ref) <= BENCH_MAX_RPM))
    return fail(err, String("RPM fuera de [-") + BENCH_MAX_RPM + ", " + BENCH_MAX_RPM + "]");
  bool ok = true;
  portENTER_CRITICAL(&mux);
  if (capState == CAP_RUNNING)
    ok = false;
  else
  {
    manualRpm = true;
    manualL = manualR = 0;
    manualRef = ref;
    manualMs = millis();
  }
  portEXIT_CRITICAL(&mux);
  if (!ok)
    err = "hay un ensayo en curso";
  return ok;
}

bool benchSetRpmConfig(const RpmConfig &n, String &err)
{
  if (!(n.kp >= 0 && n.kp <= 20))
    return fail(err, "kp debe estar en [0, 20]");
  if (!(n.ki >= 0 && n.ki <= 2000))
    return fail(err, "ki debe estar en [0, 2000]");
  if (n.tsMs < 1 || n.tsMs > 20)
    return fail(err, "ts_ms debe estar en [1, 20]");
  if (n.winMs < 1 || n.winMs > RING - 4)
    return fail(err, String("win_ms debe estar en [1, ") + (RING - 4) + "]");
  if (!(n.ffK >= 0.05f && n.ffK <= 1))
    return fail(err, "ff_k debe estar en [0.05, 1]");
  if (!(n.ffB >= 0 && n.ffB <= 20))
    return fail(err, "ff_b debe estar en [0, 20]");
  if (!(n.iMax >= 0 && n.iMax <= 255))
    return fail(err, "i_max debe estar en [0, 255]");
  if (n.refTauMs > 200)
    return fail(err, "ref_tau_ms debe estar en [0, 200]");
  portENTER_CRITICAL(&mux);
  cfg = n;
  portEXIT_CRITICAL(&mux);
  return true;
}

RpmConfig benchRpmConfig()
{
  portENTER_CRITICAL(&mux);
  const RpmConfig c = cfg;
  portEXIT_CRITICAL(&mux);
  return c;
}

bool benchRun(uint8_t motorMask, bool rpmKind, const float *val, const uint32_t *ms, uint8_t n, uint32_t periodMs,
              String &err)
{
  if (!canDrive(err))
    return false;
  if (motorMask < 1 || motorMask > 3)
    return fail(err, "motor debe ser \"L\", \"R\" o \"LR\"");
  if (n < 1 || n > BENCH_MAX_SEGMENTS)
    return fail(err, String("seq debe tener entre 1 y ") + BENCH_MAX_SEGMENTS + " tramos");
  if (periodMs < 1 || periodMs > BENCH_MAX_PERIOD_MS)
    return fail(err, String("period_ms debe estar en [1, ") + BENCH_MAX_PERIOD_MS + "]");
  const float limit = rpmKind ? BENCH_MAX_RPM : 255;
  uint32_t totalMs = 0;
  for (uint8_t i = 0; i < n; i++)
  {
    if (!(fabsf(val[i]) <= limit))
      return fail(err, rpmKind ? String("RPM fuera de [-") + BENCH_MAX_RPM + ", " + BENCH_MAX_RPM + "]"
                               : String("PWM fuera de [-255, 255]"));
    if (ms[i] < 1 || ms[i] > 60000)
      return fail(err, "cada tramo debe durar entre 1 y 60000 ms");
    totalMs += ms[i];
  }
  const uint32_t total = totalMs / periodMs + 1;
  if (total > BENCH_MAX_SAMPLES)
    return fail(err, String("el ensayo pide ") + total + " muestras; máximo " + BENCH_MAX_SAMPLES +
                         " (acortar o subir period_ms)");

  bool ok = true;
  portENTER_CRITICAL(&mux);
  if (capState == CAP_RUNNING)
    ok = false;
  else
  {
    for (uint8_t i = 0; i < n; i++)
    {
      segVal[i] = rpmKind ? val[i] : roundf(val[i]);
      segMs[i] = ms[i];
    }
    nSegs = n;
    capRpm = rpmKind;
    capMotor = motorMask;
    capPeriod = periodMs;
    capTotal = total;
    capCount = 0;
    capId++;
    capState = CAP_RUNNING;
    capStartPending = true;
    manualL = manualR = 0;
    manualRef = 0;
  }
  portEXIT_CRITICAL(&mux);
  if (!ok)
    err = "ya hay un ensayo en curso";
  return ok;
}

void benchStop()
{
  portENTER_CRITICAL(&mux);
  manualL = manualR = 0;
  manualRef = 0;
  if (capState == CAP_RUNNING)
  {
    capState = CAP_ABORTED;
    capStartPending = false;
  }
  portEXIT_CRITICAL(&mux);
}

uint32_t benchReadableSamples()
{
  uint32_t n = 0;
  portENTER_CRITICAL(&mux);
  if (buf && (capState == CAP_DONE || capState == CAP_ABORTED))
    n = capCount;
  portEXIT_CRITICAL(&mux);
  return n;
}

void benchWriteRows(Print &out, uint32_t from, uint32_t count)
{
  const uint32_t n = benchReadableSamples();
  char row[80];
  for (uint32_t i = from; i < n && i < from + count; i++)
  {
    const Sample &s = buf[i];
    int len = snprintf(row, sizeof(row), "%s[%lu,%d,%d,%ld,%ld,%d]", i == from ? "" : ",", (unsigned long)s.tUs,
                       s.pwmL, s.pwmR, (long)s.cL, (long)s.cR, s.refX10);
    out.write((const uint8_t *)row, len);
  }
}

const char *benchCapStateName(uint8_t s)
{
  switch (s)
  {
  case CAP_RUNNING:
    return "running";
  case CAP_DONE:
    return "done";
  case CAP_ABORTED:
    return "aborted";
  default:
    return "none";
  }
}

void benchStatusJson(JsonObject out)
{
  portENTER_CRITICAL(&mux);
  const bool rpm = manualRpm;
  const int16_t l = manualL, r = manualR;
  const float ref = manualRef;
  const RpmConfig c = cfg;
  const uint8_t cs = capState, motor = capMotor, n = nSegs;
  const bool kindRpm = capRpm;
  const uint16_t ci = capId;
  const uint32_t ck = capCount, cn = capTotal, period = capPeriod;
  float vals[BENCH_MAX_SEGMENTS];
  uint32_t ms[BENCH_MAX_SEGMENTS];
  memcpy(vals, segVal, sizeof(vals));
  memcpy(ms, segMs, sizeof(ms));
  portEXIT_CRITICAL(&mux);

  out["on"] = active.load();
  out["estop"] = isEStop();
  out["mode"] = rpm ? "rpm" : "pwm";
  JsonArray pwm = out["pwm"].to<JsonArray>();
  pwm.add(l);
  pwm.add(r);
  out["ref"] = ref;
  out["max_samples"] = BENCH_MAX_SAMPLES;
  out["max_rpm"] = BENCH_MAX_RPM;
  JsonObject rc = out["rpmcfg"].to<JsonObject>();
  rc["kp"] = c.kp;
  rc["ki"] = c.ki;
  rc["ts_ms"] = c.tsMs;
  rc["win_ms"] = c.winMs;
  rc["ff"] = c.ff;
  rc["ff_k"] = c.ffK;
  rc["ff_b"] = c.ffB;
  rc["i_max"] = c.iMax;
  rc["follow"] = c.follow;
  rc["ref_tau_ms"] = c.refTauMs;
  JsonObject cap = out["cap"].to<JsonObject>();
  cap["id"] = ci;
  cap["st"] = benchCapStateName(cs);
  cap["n"] = ck;
  cap["total"] = cn;
  if (cs != CAP_NONE)
  {
    cap["kind"] = kindRpm ? "rpm" : "pwm";
    cap["period_ms"] = period;
    cap["motor"] = motor == 3 ? "LR" : (motor == 1 ? "L" : "R");
    JsonArray seq = cap["seq"].to<JsonArray>();
    for (uint8_t i = 0; i < n; i++)
    {
      JsonArray s = seq.add<JsonArray>();
      if (kindRpm)
        s.add(vals[i]);
      else
        s.add((int)vals[i]);
      s.add(ms[i]);
    }
    if (kindRpm)
    {
      JsonObject used = cap["rpmcfg"].to<JsonObject>();
      used["kp"] = c.kp;
      used["ki"] = c.ki;
      used["ts_ms"] = c.tsMs;
      used["win_ms"] = c.winMs;
      used["ff"] = c.ff;
      used["follow"] = c.follow;
      used["ref_tau_ms"] = c.refTauMs;
    }
  }
}

bool receiveBenchFrame(BenchFrame &out)
{
  return frameQueue && xQueueReceive(frameQueue, &out, 0) == pdTRUE;
}

void benchLoop()
{
  static uint8_t reportedState = CAP_NONE;
  static uint16_t reportedId = 0;
  bool expired;
  portENTER_CRITICAL(&mux);
  expired = manualExpired;
  manualExpired = false;
  const uint8_t cs = capState;
  const uint16_t ci = capId;
  const uint32_t ck = capCount;
  portEXIT_CRITICAL(&mux);

  if (expired)
    Console.printf(">> Banco: mando manual apagado (no se renovó en %lu s).\n",
                   (unsigned long)(BENCH_MANUAL_TIMEOUT_MS / 1000));
  if ((cs == CAP_DONE || cs == CAP_ABORTED) && (cs != reportedState || ci != reportedId))
    Console.printf(">> Banco: ensayo %u %s (%lu muestras).\n", ci, cs == CAP_DONE ? "terminado" : "abortado",
                   (unsigned long)ck);
  reportedState = cs;
  reportedId = ci;
}
