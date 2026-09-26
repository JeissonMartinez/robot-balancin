#include "motors.h"
#include "config.h"
#include <driver/ledc.h>

static void setupMotorPWM(int pwmPin, ledc_channel_t pwmChannel, ledc_timer_t timerNum)
{
  ledc_timer_config_t timer_conf = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = timerNum,
      .freq_hz = PWM_FREQ,
      .clk_cfg = LEDC_AUTO_CLK};

  ledc_timer_config(&timer_conf);

  ledc_channel_config_t channel_conf = {
      .gpio_num = pwmPin,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = pwmChannel,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = timerNum,
      .duty = 0};

  ledc_channel_config(&channel_conf);
}

static void setMotor(int in1, int in2, ledc_channel_t channel, float pwm)
{
  int duty = constrain((int)fabsf(pwm), 0, 255);

  if (pwm > 0)
  { // Adelante
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  }
  else
  { // Atrás
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void setupMotors()
{
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, LOW);

  setupMotorPWM(PWMA, CH_A, TIMER_A);
  setupMotorPWM(PWMB, CH_B, TIMER_B);
}

void driveMotorsDifferential(float pwmL, float pwmR)
{
  setMotor(AIN1, AIN2, CH_A, pwmL); // Motor izquierdo
  setMotor(BIN1, BIN2, CH_B, pwmR); // Motor derecho
}

float compensateDeadband(float pwm, float deadband)
{
  if (deadband <= 0.0f)
    return pwm;

  const float scale = (255.0f - deadband) / 255.0f;
  float mag = fabsf(pwm);
  float out;
  if (mag >= PWM_DEADBAND_BLEND)
    out = deadband + mag * scale;
  else
    out = mag * (deadband + PWM_DEADBAND_BLEND * scale) / PWM_DEADBAND_BLEND;

  return (pwm >= 0.0f) ? out : -out;
}

void enableMotors()
{
  digitalWrite(STBY, HIGH);
}

void stopMotors()
{
  ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_A, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_A);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_B, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_B);
  digitalWrite(STBY, LOW);
}
