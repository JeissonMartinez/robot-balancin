#include "tasks_block.h"
#include "config.h"
#include "encoders.h"
#include "motors.h"
#include "mpu_block.h"
#include "nn_cascade_block.h"
#include <atomic>
#include <esp_timer.h>

static TaskHandle_t taskBalHandle = NULL;

static std::atomic<bool> pauseRequested(false);
static std::atomic<bool> paused(false);

static Telemetry telemetry = {};
static portMUX_TYPE telemetryMux = portMUX_INITIALIZER_UNLOCKED;

static void publishTelemetry(const Telemetry &t)
{
  portENTER_CRITICAL(&telemetryMux);
  telemetry = t;
  portEXIT_CRITICAL(&telemetryMux);
}

static void TaskBalanceo(void *pvParameters)
{
  const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
  const float nominalDt = CONTROL_PERIOD_MS / 1000.0f;

  TickType_t lastWake = xTaskGetTickCount();
  int64_t lastUs = esp_timer_get_time();
  bool active = false;
  bool wasPaused = false;

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

    // --- dt medido ---
    int64_t nowUs = esp_timer_get_time();
    float dt = (nowUs - lastUs) * 1e-6f;
    lastUs = nowUs;
    if (dt <= 0.0f || dt > 5.0f * nominalDt)
      dt = nominalDt;

    float angle = updateAngle(dt);
    float rpmL_f, rpmR_f;
    updateWheelRPM(dt, rpmL_f, rpmR_f);

    // --- Seguridad: caída / re-armado con histéresis ---
    if (active && fabsf(angle) > MAX_ANGLE)
    {
      active = false;
      stopMotors();
    }
    else if (!active && fabsf(angle) < REARM_ANGLE)
    {
      active = true;
      resetCascade(angle);
      resetWheelRPM();
      rpmL_f = rpmR_f = 0;
      enableMotors();
    }

    float pwm = 0;
    if (active)
    {
      pwm = cascada(angle, getAngularRate(), rpmL_f, rpmR_f, dt);
      float pwmMotor = compensateDeadband(pwm);
      driveMotorsDifferential(pwmMotor, pwmMotor);
    }

    publishTelemetry({angle, getAngleReference(), getAngularRate(), pwm, rpmL_f, rpmR_f, Kp_angle, dt, active});

    vTaskDelayUntil(&lastWake, period);
  }
}

void startControlTask()
{
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

void getTelemetry(Telemetry &out)
{
  portENTER_CRITICAL(&telemetryMux);
  out = telemetry;
  portEXIT_CRITICAL(&telemetryMux);
}
