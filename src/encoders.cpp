#include "encoders.h"
#include "config.h"
#include <driver/pcnt.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>

// El contador de 16 bits vuelve a 0 al llegar a ±PCNT_LIMIT. Se lee cada 10 ms (y en cada
// consulta) y la diferencia módulo PCNT_LIMIT se suma a un total de 32 bits: no se pierde
// ninguna cuenta mientras entre dos lecturas haya menos de PCNT_LIMIT/2 (≈ 2.4 s a 70 RPM).
static const int16_t PCNT_LIMIT = 30000;
static const uint32_t POLL_US = 10000;
static const uint16_t GLITCH_CYCLES = ENC_GLITCH_NS * 80 / 1000; // reloj APB de 80 MHz

static const pcnt_unit_t UNIT_L = PCNT_UNIT_0;
static const pcnt_unit_t UNIT_R = PCNT_UNIT_1;
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
static int16_t prevRaw[2] = {0, 0};
static int32_t total[2] = {0, 0}; // [izquierdo, derecho], sin ENC_*_SIGN

static int32_t lastL = 0; // total en la última llamada a readAndResetEncoders
static int32_t lastR = 0;
static float rpmL_filtered = 0;
static float rpmR_filtered = 0;

static const uint8_t EDGE_PINS[4] = {L_A, L_B, R_A, R_B};
static volatile uint32_t edges[4] = {0, 0, 0, 0};
static bool edgeCountOn = false;

// Lectura directa del registro de entrada GPIO (0..31 en GPIO_IN, 32..48 en GPIO_IN1)
static inline int fastRead(uint8_t pin)
{
  if (pin < 32)
    return (REG_READ(GPIO_IN_REG) >> pin) & 1;
  return (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
}

static void configChannel(pcnt_unit_t unit, pcnt_channel_t ch, int edgePin, int levelPin, pcnt_count_mode_t pos,
                          pcnt_count_mode_t neg)
{
  pcnt_config_t c = {};
  c.pulse_gpio_num = edgePin;
  c.ctrl_gpio_num = levelPin;
  c.hctrl_mode = PCNT_MODE_KEEP;    // nivel alto del otro canal: el sentido de pos/neg
  c.lctrl_mode = PCNT_MODE_REVERSE; // nivel bajo: sentido contrario
  c.pos_mode = pos;
  c.neg_mode = neg;
  c.counter_h_lim = PCNT_LIMIT;
  c.counter_l_lim = -PCNT_LIMIT;
  c.unit = unit;
  c.channel = ch;
  ESP_ERROR_CHECK(pcnt_unit_config(&c));
}

/**
 * Unidad PCNT en cuadratura x4. Canal 0: flancos de A, nivel de B; canal 1: flancos de B,
 * nivel de A. Las acciones dan −1 por flanco cuando A adelanta a B, igual que la
 * interrupción de la versión anterior (step = A == B ? +1 : −1 en cada flanco de A).
 */
static void setupQuadratureUnit(pcnt_unit_t unit, int pinA, int pinB)
{
  configChannel(unit, PCNT_CHANNEL_0, pinA, pinB, PCNT_COUNT_INC, PCNT_COUNT_DEC);
  configChannel(unit, PCNT_CHANNEL_1, pinB, pinA, PCNT_COUNT_DEC, PCNT_COUNT_INC);
  ESP_ERROR_CHECK(pcnt_set_filter_value(unit, GLITCH_CYCLES));
  ESP_ERROR_CHECK(pcnt_filter_enable(unit));
  ESP_ERROR_CHECK(pcnt_counter_pause(unit));
  ESP_ERROR_CHECK(pcnt_counter_clear(unit));
  ESP_ERROR_CHECK(pcnt_counter_resume(unit));
}

static inline int32_t wrapDelta(int32_t d)
{
  if (d > PCNT_LIMIT / 2)
    return d - PCNT_LIMIT;
  if (d < -PCNT_LIMIT / 2)
    return d + PCNT_LIMIT;
  return d;
}

// Actualiza los totales. Llamar con encMux tomado.
static void pollLocked()
{
  const pcnt_unit_t units[2] = {UNIT_L, UNIT_R};
  for (int i = 0; i < 2; i++)
  {
    int16_t v = 0;
    pcnt_get_counter_value(units[i], &v);
    total[i] += wrapDelta((int32_t)v - prevRaw[i]);
    prevRaw[i] = v;
  }
}

static void rawCounts(int32_t &l, int32_t &r)
{
  portENTER_CRITICAL(&encMux);
  pollLocked();
  l = total[0];
  r = total[1];
  portEXIT_CRITICAL(&encMux);
}

static void pollTimer(void *)
{
  int32_t l, r;
  rawCounts(l, r);
}

void setupEncoders()
{
  // Pull-up antes de entregar los pines al PCNT (por si el driver no los activa)
  pinMode(R_A, INPUT_PULLUP);
  pinMode(R_B, INPUT_PULLUP);
  pinMode(L_A, INPUT_PULLUP);
  pinMode(L_B, INPUT_PULLUP);

  setupQuadratureUnit(UNIT_L, L_A, L_B);
  setupQuadratureUnit(UNIT_R, R_A, R_B);

  esp_timer_create_args_t args = {};
  args.callback = pollTimer;
  args.name = "enc_poll";
  esp_timer_handle_t timer = nullptr;
  ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(timer, POLL_US));
}

void readAndResetEncoders(long &pulsesL, long &pulsesR)
{
  portENTER_CRITICAL(&encMux);
  pollLocked();
  pulsesL = total[0] - lastL;
  pulsesR = total[1] - lastR;
  lastL = total[0];
  lastR = total[1];
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

void encoderCounts(int32_t &countL, int32_t &countR)
{
  int32_t l, r;
  rawCounts(l, r);
  countL = ENC_L_SIGN * l;
  countR = ENC_R_SIGN * r;
}

// ===================== DIAGNÓSTICO: FLANCOS POR CANAL ===========
// Un pin puede ir a la vez al PCNT (por la matriz GPIO) y generar su interrupción.
static void IRAM_ATTR isrEdge(void *arg)
{
  edges[(uint32_t)arg]++; // un solo escritor por índice
}

void encoderEdgeCount(bool on)
{
  if (on == edgeCountOn)
    return;
  edgeCountOn = on;
  for (uint32_t i = 0; i < 4; i++)
  {
    if (on)
    {
      edges[i] = 0;
      attachInterruptArg(EDGE_PINS[i], isrEdge, (void *)i, CHANGE);
    }
    else
      detachInterrupt(EDGE_PINS[i]);
  }
}

void encoderEdges(uint32_t out[4])
{
  for (int i = 0; i < 4; i++)
    out[i] = edges[i];
}

uint8_t encoderLevels()
{
  uint8_t v = 0;
  for (int i = 0; i < 4; i++)
    v |= fastRead(EDGE_PINS[i]) << i;
  return v;
}
