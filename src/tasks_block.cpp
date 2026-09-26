#include "tasks_block.h"
#include "config.h"
#include "encoders.h"
#include "motors.h"
#include "mpu_block.h"
#include "nn_cascade_block.h"
#include "params.h"
#include <atomic>
#include <esp_timer.h>

static TaskHandle_t taskBalHandle = NULL;
static QueueHandle_t telemetryQueue = NULL;
static const UBaseType_t TELEMETRY_QUEUE_LEN = 25; // 0.5 s a 50 Hz

static std::atomic<bool> pauseRequested(false);
static std::atomic<bool> paused(false);
static std::atomic<bool> estop(false);
static std::atomic<uint32_t> dropped(0);

static void TaskBalanceo(void *pvParameters)
{
  const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
  const float nominalDt = CONTROL_PERIOD_MS / 1000.0f;

  TickType_t lastWake = xTaskGetTickCount();
  int64_t lastUs = esp_timer_get_time();
  bool active = false;
  bool wasPaused = false;
  uint32_t seq = 0;

  Params p;
  uint32_t paramsVersion = paramsSnapshot(p);
  uint8_t appliedDlpf = p.mpuDlpfMode;
  ControlStructure appliedStructure = p.structure;

  while (true)
  {
    // --- Pausa segura (calibración): se atiende al inicio del ciclo, sin I2C en curso ---
    if (pauseRequested.load())
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
      resetWheelRPM();
      rpmL_f = rpmR_f = 0;
      enableMotors();
    }
    else if (active && structureChanged)
    {
      resetCascade(p, angle);
    }

    float pwm = 0, pwmMotor = 0;
    if (active)
    {
      pwm = cascada(p, angle, getAngularRate(), rpmL_f, rpmR_f, dt);
      pwmMotor = compensateDeadband(pwm, p.pwmDeadband);
      driveMotorsDifferential(pwmMotor, pwmMotor);
    }

    RobotState state = active ? RobotState::Active : (stopNow ? RobotState::EStop : RobotState::Idle);
    Telemetry t = {seq++, millis(), angle, getAngleReference(), getAngularRate(), pwm, pwmMotor,
                   rpmL_f, rpmR_f, getKpAngle(), dt, state};
    if (xQueueSend(telemetryQueue, &t, 0) != pdTRUE)
      dropped.fetch_add(1);

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
  pauseRequested.store(true);
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
  pauseRequested.store(false);
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
