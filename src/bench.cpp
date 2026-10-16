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
};

enum CapState : uint8_t
{
  CAP_NONE = 0,
  CAP_RUNNING = 1,
  CAP_DONE = 2,
  CAP_ABORTED = 3
};

TaskHandle_t task = nullptr;
QueueHandle_t frameQueue = nullptr;
const UBaseType_t FRAME_QUEUE_LEN = 25; // 0.5 s a 50 Hz
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> active(false);
std::atomic<bool> taskIdle(true);

// --- Mando: lo escribe loop() y lo lee la tarea, siempre con `mux` ---
int16_t manualL = 0, manualR = 0;
uint32_t manualMs = 0;
bool manualExpired = false;

Sample *buf = nullptr;
int16_t segPwm[BENCH_MAX_SEGMENTS];
uint32_t segMs[BENCH_MAX_SEGMENTS];
uint8_t nSegs = 0;
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

// PWM del tramo que contiene el instante t [ms] del ensayo; 0 después del último
int16_t segmentAt(uint32_t tMs)
{
  uint32_t end = 0;
  for (uint8_t i = 0; i < nSegs; i++)
  {
    end += segMs[i];
    if (tMs < end)
      return segPwm[i];
  }
  return 0;
}

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
      continue;
    }

    vTaskDelayUntil(&lastWake, 1);
    tick++;
    const int64_t nowUs = esp_timer_get_time();
    int32_t cL, cR;
    encoderCounts(cL, cR);
    const bool estop = isEStop();

    int16_t uL, uR;
    portENTER_CRITICAL(&mux);
    if (estop)
    {
      manualL = manualR = 0;
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
        const uint32_t k = elapsed / capPeriod;
        const int16_t u = (k + 1 >= capTotal) ? 0 : segmentAt(k * capPeriod);
        capUL = (capMotor & 1) ? u : 0;
        capUR = (capMotor & 2) ? u : 0;
        buf[k] = {(uint32_t)(nowUs - capUs0), capUL, capUR, cL - capL0, cR - capR0};
        capCount = k + 1;
        if (capCount >= capTotal)
          capState = CAP_DONE;
      }
      uL = capUL;
      uR = capUR;
    }
    else
    {
      if ((manualL || manualR) && millis() - manualMs > BENCH_MANUAL_TIMEOUT_MS)
      {
        manualL = manualR = 0;
        manualExpired = true;
      }
      uL = manualL;
      uR = manualR;
    }
    const uint8_t cs = capState;
    const uint16_t ci = capId;
    const uint32_t ck = capCount, cn = capTotal;
    portEXIT_CRITICAL(&mux);

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
      f.capState = cs;
      f.capId = ci;
      f.capCount = ck;
      f.capTotal = cn;
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
  xTaskCreatePinnedToCore(TaskBanco, "TaskBanco", 4096, NULL, 4, &task, 1);
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
  manualL = manualR = 0;
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

bool benchSetPwm(int pwmL, int pwmR, String &err)
{
  if (!active.load())
    return fail(err, "el banco no está activo");
  if (isEStop())
    return fail(err, "parada de emergencia activa: Armar primero");
  if (pwmL < -255 || pwmL > 255 || pwmR < -255 || pwmR > 255)
    return fail(err, "PWM fuera de [-255, 255]");
  bool ok = true;
  portENTER_CRITICAL(&mux);
  if (capState == CAP_RUNNING)
    ok = false;
  else
  {
    manualL = pwmL;
    manualR = pwmR;
    manualMs = millis();
  }
  portEXIT_CRITICAL(&mux);
  if (!ok)
    err = "hay un ensayo en curso";
  return ok;
}

bool benchRun(uint8_t motorMask, const int16_t *pwm, const uint32_t *ms, uint8_t n, uint32_t periodMs, String &err)
{
  if (!active.load())
    return fail(err, "el banco no está activo");
  if (isEStop())
    return fail(err, "parada de emergencia activa: Armar primero");
  if (motorMask < 1 || motorMask > 3)
    return fail(err, "motor debe ser \"L\", \"R\" o \"LR\"");
  if (n < 1 || n > BENCH_MAX_SEGMENTS)
    return fail(err, String("seq debe tener entre 1 y ") + BENCH_MAX_SEGMENTS + " tramos");
  if (periodMs < 1 || periodMs > BENCH_MAX_PERIOD_MS)
    return fail(err, String("period_ms debe estar en [1, ") + BENCH_MAX_PERIOD_MS + "]");
  uint32_t totalMs = 0;
  for (uint8_t i = 0; i < n; i++)
  {
    if (pwm[i] < -255 || pwm[i] > 255)
      return fail(err, "PWM fuera de [-255, 255]");
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
      segPwm[i] = pwm[i];
      segMs[i] = ms[i];
    }
    nSegs = n;
    capMotor = motorMask;
    capPeriod = periodMs;
    capTotal = total;
    capCount = 0;
    capId++;
    capState = CAP_RUNNING;
    capStartPending = true;
    manualL = manualR = 0;
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
  char row[64];
  for (uint32_t i = from; i < n && i < from + count; i++)
  {
    const Sample &s = buf[i];
    int len = snprintf(row, sizeof(row), "%s[%lu,%d,%d,%ld,%ld]", i == from ? "" : ",", (unsigned long)s.tUs, s.pwmL,
                       s.pwmR, (long)s.cL, (long)s.cR);
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
  const int16_t l = manualL, r = manualR;
  const uint8_t cs = capState, motor = capMotor, n = nSegs;
  const uint16_t ci = capId;
  const uint32_t ck = capCount, cn = capTotal, period = capPeriod;
  int16_t pw[BENCH_MAX_SEGMENTS];
  uint32_t ms[BENCH_MAX_SEGMENTS];
  memcpy(pw, segPwm, sizeof(pw));
  memcpy(ms, segMs, sizeof(ms));
  portEXIT_CRITICAL(&mux);

  out["on"] = active.load();
  out["estop"] = isEStop();
  JsonArray pwm = out["pwm"].to<JsonArray>();
  pwm.add(l);
  pwm.add(r);
  out["max_samples"] = BENCH_MAX_SAMPLES;
  JsonObject cap = out["cap"].to<JsonObject>();
  cap["id"] = ci;
  cap["st"] = benchCapStateName(cs);
  cap["n"] = ck;
  cap["total"] = cn;
  if (cs != CAP_NONE)
  {
    cap["period_ms"] = period;
    cap["motor"] = motor == 3 ? "LR" : (motor == 1 ? "L" : "R");
    JsonArray seq = cap["seq"].to<JsonArray>();
    for (uint8_t i = 0; i < n; i++)
    {
      JsonArray s = seq.add<JsonArray>();
      s.add(pw[i]);
      s.add(ms[i]);
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
    Console.printf(">> Banco: PWM manual apagado (no se renovó en %lu s).\n",
                   (unsigned long)(BENCH_MANUAL_TIMEOUT_MS / 1000));
  if ((cs == CAP_DONE || cs == CAP_ABORTED) && (cs != reportedState || ci != reportedId))
    Console.printf(">> Banco: ensayo %u %s (%lu muestras).\n", ci, cs == CAP_DONE ? "terminado" : "abortado",
                   (unsigned long)ck);
  reportedState = cs;
  reportedId = ci;
}
