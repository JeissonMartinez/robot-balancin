#include "encoders.h"
#include "config.h"
#include <soc/gpio_reg.h>

static volatile long countR = 0;
static volatile long countL = 0;
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;

static float rpmL_filtered = 0;
static float rpmR_filtered = 0;

// Lectura directa del registro de entrada GPIO (0..31 en GPIO_IN, 32..48 en GPIO_IN1)
static inline __attribute__((always_inline)) int fastRead(uint8_t pin)
{
  if (pin < 32)
    return (REG_READ(GPIO_IN_REG) >> pin) & 1;
  return (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
}

// ===================== ISRs ENCODER ============================
static void IRAM_ATTR isr_RA()
{
  int step = (fastRead(R_A) == fastRead(R_B)) ? 1 : -1;
  portENTER_CRITICAL_ISR(&encMux);
  countR += step;
  portEXIT_CRITICAL_ISR(&encMux);
}

static void IRAM_ATTR isr_LA()
{
  int step = (fastRead(L_A) == fastRead(L_B)) ? 1 : -1;
  portENTER_CRITICAL_ISR(&encMux);
  countL += step;
  portEXIT_CRITICAL_ISR(&encMux);
}

void setupEncoders()
{
  pinMode(R_A, INPUT_PULLUP);
  pinMode(R_B, INPUT_PULLUP);
  pinMode(L_A, INPUT_PULLUP);
  pinMode(L_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(R_A), isr_RA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(L_A), isr_LA, CHANGE);
}

void readAndResetEncoders(long &pulsesL, long &pulsesR)
{
  portENTER_CRITICAL(&encMux);
  pulsesL = countL;
  pulsesR = countR;
  countL = 0;
  countR = 0;
  portEXIT_CRITICAL(&encMux);
}

void updateWheelRPM(float dt, float &rpmL_f, float &rpmR_f)
{
  long pulsesL, pulsesR;
  readAndResetEncoders(pulsesL, pulsesR);

  float rpmL = ENC_L_SIGN * (pulsesL / PPR_WHEEL / dt) * 60.0f;
  float rpmR = ENC_R_SIGN * (pulsesR / PPR_WHEEL / dt) * 60.0f;

  rpmL_filtered = 0.7f * rpmL_filtered + 0.3f * rpmL;
  rpmR_filtered = 0.7f * rpmR_filtered + 0.3f * rpmR;

  rpmL_f = rpmL_filtered;
  rpmR_f = rpmR_filtered;
}

void resetWheelRPM()
{
  long discardL, discardR;
  readAndResetEncoders(discardL, discardR);
  rpmL_filtered = 0;
  rpmR_filtered = 0;
}
