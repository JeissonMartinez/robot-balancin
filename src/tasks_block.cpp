#include "tasks_block.h"
#include "config.h"
#include "encoders.h"
#include "motors.h"
#include "mpu_block.h"
#include "nn_cascade_block.h"
#include "params.h"
#include "rpm_cascade.h"
#include <atomic>
#include <esp_timer.h>

static TaskHandle_t taskBalHandle = NULL;
static QueueHandle_t telemetryQueue = NULL;
static const UBaseType_t TELEMETRY_QUEUE_LEN = 25; // 0.5 s a 50 Hz

static std::atomic<int> pauseRequests(0); // pausas pedidas y no liberadas (banco + comandos)
static std::atomic<bool> paused(false);
static std::atomic<bool> estop(false);
static std::atomic<uint32_t> dropped(0);

static uint32_t periodFor(ControlStructure s)
{
  return s == ControlStructure::RpmCascade ? CASCADE_PERIOD_MS : CONTROL_PERIOD_MS;
}

static void TaskBalanceo(void *pvParameters)
{
  TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
  float nominalDt = CONTROL_PERIOD_MS / 1000.0f;
  uint32_t periodMs = CONTROL_PERIOD_MS;
  uint32_t telAccMs = 0;

  TickType_t lastWake = xTaskGetTickCount();
  int64_t lastUs = esp_timer_get_time();
  bool active = false;
  bool wasPaused = false;
  uint32_t seq = 0;

  Params p;
  uint32_t paramsVersion = paramsSnapshot(p);
  uint8_t appliedDlpf = p.mpuDlpfMode;
  ControlStructure appliedStructure = p.structure;
  periodMs = periodFor(p.structure);
  period = pdMS_TO_TICKS(periodMs);
  nominalDt = periodMs / 1000.0f;

  while (true)
  {
    // --- Pausa segura (calibración): se atiende al inicio del ciclo, sin I2C en curso ---
    if (pauseRequests.load() > 0)
    {
      if (!wasPaused)
      {
        stopMotors();
        active = false;
        wasPaused = true;
        paused.store(true);
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (wasPaused)
    {
      wasPaused = false;
      resetAngleFromAccel();
      resetWheelRPM();
      lastWake = xTaskGetTickCount();
      lastUs = esp_timer_get_time();
      paused.store(false);
      vTaskDelayUntil(&lastWake, period);
    }

    // --- Parámetros del ciclo ---
    uint32_t v = paramsSnapshot(p);
    bool structureChanged = false;
    if (v != paramsVersion)
    {
      paramsVersion = v;
      if (p.mpuDlpfMode != appliedDlpf)
      {
        setMPUFilter(p.mpuDlpfMode);
        appliedDlpf = p.mpuDlpfMode;
      }
      structureChanged = p.structure != appliedStructure;
      appliedStructure = p.structure;
      if (structureChanged)
      {
        periodMs = periodFor(p.structure);
        period = pdMS_TO_TICKS(periodMs);
        nominalDt = periodMs / 1000.0f;
      }
    }

    // --- dt medido ---
    int64_t nowUs = esp_timer_get_time();
    float dt = (nowUs - lastUs) * 1e-6f;
    lastUs = nowUs;
    if (dt <= 0.0f || dt > 5.0f * nominalDt)
      dt = nominalDt;

    float angle = updateAngle(dt);
    float rpmL_f, rpmR_f;
    updateWheelRPM(dt, rpmL_f, rpmR_f);

    // --- Seguridad: parada de emergencia, caída / re-armado con histéresis ---
    bool stopNow = estop.load();
    if (active && (stopNow || fabsf(angle) > p.maxAngle))
    {
      active = false;
      stopMotors();
    }
    else if (!active && !stopNow && fabsf(angle) < p.rearmAngle)
    {
      active = true;
      resetCascade(p, angle);
      rpmCascadeReset();
      resetWheelRPM();
      rpmL_f = rpmR_f = 0;
      enableMotors();
    }
    else if (active && structureChanged)
    {
      resetCascade(p, angle);
      rpmCascadeReset();
    }

    float pwm = 0, pwmMotor = 0;
    ControlTerms terms = {};
    CascadeState cs = {};
    const bool rpmCascade = p.structure == ControlStructure::RpmCascade;
    if (active && rpmCascade)
    {
      // M3: la prealimentación del lazo de RPM ya compensa la zona muerta
      pwm = pwmMotor = rpmCascadeStep(p, angle, getAngularRate(), periodMs, dt);
      driveMotorsDifferential(pwmMotor, pwmMotor); // izquierdo: seguidor
      cs = rpmCascadeState();
      terms = {cs.uP, cs.uI, 0.0f};
    }
    else if (active)
    {
      pwm = cascada(p, angle, getAngularRate(), rpmL_f, rpmR_f, dt);
      pwmMotor = compensateDeadband(pwm, p.pwmDeadband);
      driveMotorsDifferential(pwmMotor, pwmMotor);
      terms = getControlTerms();
    }

    telAccMs += periodMs;
    if (telAccMs >= TELEMETRY_PERIOD_MS)
    {
      telAccMs -= TELEMETRY_PERIOD_MS;
      RobotState state = active ? RobotState::Active : (stopNow ? RobotState::EStop : RobotState::Idle);
      Telemetry t = {seq++, millis(), angle, rpmCascade ? p.setpointAngle : getAngleReference(), getAngularRate(),
                     pwm, pwmMotor, rpmL_f, rpmR_f, getKpAngle(), dt, terms.p, terms.i, terms.d,
                     cs.rpmRef, cs.accel, cs.rpmMeas, cs.uF, state};
      if (xQueueSend(telemetryQueue, &t, 0) != pdTRUE)
        dropped.fetch_add(1);
    }

    vTaskDelayUntil(&lastWake, period);
  }
}

void startControlTask()
{
  telemetryQueue = xQueueCreate(TELEMETRY_QUEUE_LEN, sizeof(Telemetry));
  xTaskCreatePinnedToCore(TaskBalanceo, "TaskBalanceo", 10000, NULL, 3, &taskBalHandle, 1);
}

bool pauseControl(uint32_t timeoutMs)
{
  pauseRequests.fetch_add(1);
  uint32_t start = millis();
  while (!paused.load())
  {
    if (millis() - start > timeoutMs)
      return false;
    delay(2);
  }
  return true;
}

void resumeControl()
{
  int n = pauseRequests.load();
  while (n > 0 && !pauseRequests.compare_exchange_weak(n, n - 1))
    ;
}

bool runWithControlPaused(void (*action)())
{
  if (!pauseControl())
  {
    resumeControl();
    return false;
  }
  action();
  resumeControl();
  return true;
}

void setEStop(bool on)
{
  estop.store(on);
}

bool isEStop()
{
  return estop.load();
}

bool receiveTelemetry(Telemetry &out)
{
  return telemetryQueue && xQueueReceive(telemetryQueue, &out, 0) == pdTRUE;
}

uint32_t droppedTelemetry()
{
  return dropped.load();
}

const char *robotStateName(RobotState s)
{
  switch (s)
  {
  case RobotState::Active:
    return "ACTIVE";
  case RobotState::EStop:
    return "ESTOP";
  default:
    return "IDLE";
  }
}
