#include "params.h"
#include <Preferences.h>
#include <stddef.h>

// Cambiar si se modifica la estructura Params: invalida lo guardado en NVS.
static const uint16_t PARAMS_LAYOUT_VERSION = 1;

enum class ParamType : uint8_t
{
  Float,
  Int,       // uint8_t
  Bool,
  Structure, // ControlStructure, en JSON como texto
  Sign       // float, sólo -1 o +1
};

struct ParamDesc
{
  const char *key;
  ParamType type;
  size_t offset;
  float min, max;
  const char *unit;
  const char *desc;
};

#define P(field) offsetof(Params, field)

static const ParamDesc DESCS[] = {
    {"structure", ParamType::Structure, P(structure), 0, 1, "", "Estructura de control (SpeedOuter / AngleOuter). Reinicia el controlador."},
    {"setpoint_angle", ParamType::Float, P(setpointAngle), -10, 10, "°", "Referencia del lazo de ángulo."},
    {"kp_min", ParamType::Float, P(kpMin), 0, 150, "", "Kp mínima que puede elegir la RN."},
    {"kp_max", ParamType::Float, P(kpMax), 0, 150, "", "Kp máxima que puede elegir la RN (= kp_min: Kp fija)."},
    {"kd_angle", ParamType::Float, P(kdAngle), 0, 5, "", "Acción derivativa del lazo de ángulo."},
    {"ki_angle", ParamType::Float, P(kiAngle), 0, 10, "", "Integral del lazo de ángulo (AngleOuter)."},
    {"kp_speed", ParamType::Float, P(kpSpeed), 0, 3, "", "Proporcional del lazo interno de velocidad (AngleOuter)."},
    {"ki_speed", ParamType::Float, P(kiSpeed), 0, 2, "", "Integral del lazo interno de velocidad (AngleOuter)."},
    {"kp_v", ParamType::Float, P(kpV), 0, 0.5, "°/RPM", "Proporcional del lazo externo de velocidad (SpeedOuter)."},
    {"ki_v", ParamType::Float, P(kiV), 0, 0.5, "°/(RPM·s)", "Integral del lazo externo de velocidad (SpeedOuter)."},
    {"speed_sign", ParamType::Sign, P(speedLoopSign), -1, 1, "", "Signo del lazo externo de velocidad (SpeedOuter)."},
    {"speed_ref", ParamType::Float, P(speedRefRpm), -30, 30, "RPM", "Velocidad deseada (SpeedOuter)."},
    {"max_tilt_ref", ParamType::Float, P(maxTiltRefDeg), 0, 10, "°", "Límite del ángulo deseado que pide el lazo de velocidad."},
    {"pwm_deadband", ParamType::Float, P(pwmDeadband), 0, 40, "PWM", "Compensación de zona muerta (0 = desactivada)."},
    {"nn_error_band", ParamType::Float, P(nnErrorBand), 0, 5, "°", "La RN sube Kp si |e| supera esta banda y la baja si no."},
    {"nn_lr", ParamType::Float, P(nnLearningRate), 0, 3, "", "Tasa de aprendizaje de la RN."},
    {"kp_tau", ParamType::Float, P(kpFilterTau), 0, 2, "s", "Constante de tiempo del filtro de Kp."},
    {"max_angle", ParamType::Float, P(maxAngle), 10, 60, "°", "Ángulo de caída: motores off."},
    {"rearm_angle", ParamType::Float, P(rearmAngle), 1, 15, "°", "Ángulo de reactivación del control."},
    {"mpu_dlpf", ParamType::Int, P(mpuDlpfMode), 0, 6, "", "Filtro del MPU6050: 0=256, 1=188, 2=98, 3=42, 4=20, 5=10, 6=5 Hz."},
    {"gyro_deriv", ParamType::Bool, P(useGyroDerivative), 0, 1, "", "Derivada con el giroscopio (-ω) en vez de diferenciar el error."},
};
static const size_t N_DESCS = sizeof(DESCS) / sizeof(DESCS[0]);

#undef P

static Params active;
static uint32_t version = 0;
static bool fromNvs = false;
static portMUX_TYPE paramsMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------------
// Acceso genérico a un campo por su descriptor (como float, para validar rangos)
// ---------------------------------------------------------------------------------
static float readField(const Params &p, const ParamDesc &d)
{
  const uint8_t *base = reinterpret_cast<const uint8_t *>(&p) + d.offset;
  switch (d.type)
  {
  case ParamType::Int:
    return *reinterpret_cast<const uint8_t *>(base);
  case ParamType::Bool:
    return *reinterpret_cast<const bool *>(base) ? 1 : 0;
  case ParamType::Structure:
    return (float)(int)*reinterpret_cast<const ControlStructure *>(base);
  default:
    return *reinterpret_cast<const float *>(base);
  }
}

static void writeField(Params &p, const ParamDesc &d, float v)
{
  uint8_t *base = reinterpret_cast<uint8_t *>(&p) + d.offset;
  switch (d.type)
  {
  case ParamType::Int:
    *reinterpret_cast<uint8_t *>(base) = (uint8_t)v;
    break;
  case ParamType::Bool:
    *reinterpret_cast<bool *>(base) = v != 0;
    break;
  case ParamType::Structure:
    *reinterpret_cast<ControlStructure *>(base) = (ControlStructure)(int)v;
    break;
  default:
    *reinterpret_cast<float *>(base) = v;
  }
}

static const ParamDesc *findDesc(const char *key)
{
  for (size_t i = 0; i < N_DESCS; i++)
    if (strcmp(DESCS[i].key, key) == 0)
      return &DESCS[i];
  return nullptr;
}

const char *structureName(ControlStructure s)
{
  return s == ControlStructure::SpeedOuter ? "SpeedOuter" : "AngleOuter";
}

static bool parseStructure(const char *name, ControlStructure &out)
{
  if (strcmp(name, "SpeedOuter") == 0)
    out = ControlStructure::SpeedOuter;
  else if (strcmp(name, "AngleOuter") == 0)
    out = ControlStructure::AngleOuter;
  else
    return false;
  return true;
}

// Rangos de cada campo y reglas entre campos.
static bool validate(const Params &p, String &err)
{
  for (size_t i = 0; i < N_DESCS; i++)
  {
    const ParamDesc &d = DESCS[i];
    float v = readField(p, d);
    if (!isfinite(v) || v < d.min || v > d.max)
    {
      err = String(d.key) + " fuera de rango [" + String(d.min, 3) + ", " + String(d.max, 3) + "]";
      return false;
    }
    if (d.type == ParamType::Sign && v != 1.0f && v != -1.0f)
    {
      err = String(d.key) + " debe ser -1 o 1";
      return false;
    }
  }
  if (p.kpMin > p.kpMax)
  {
    err = "kp_min debe ser <= kp_max";
    return false;
  }
  if (p.rearmAngle >= p.maxAngle)
  {
    err = "rearm_angle debe ser < max_angle";
    return false;
  }
  return true;
}

Params paramsFactory()
{
  Params p;
  p.structure = CONTROL_STRUCTURE;
  p.setpointAngle = SETPOINT_ANGLE;
  p.kpMin = KP_MIN;
  p.kpMax = KP_MAX;
  p.kdAngle = KD_ANGLE;
  p.kiAngle = KI_ANGLE;
  p.kpSpeed = KP_SPEED;
  p.kiSpeed = KI_SPEED;
  p.kpV = KP_V;
  p.kiV = KI_V;
  p.speedLoopSign = SPEED_LOOP_SIGN;
  p.speedRefRpm = SPEED_REF_RPM;
  p.maxTiltRefDeg = MAX_TILT_REF_DEG;
  p.pwmDeadband = PWM_DEADBAND;
  p.nnErrorBand = NN_ERROR_BAND;
  p.nnLearningRate = NN_LEARNING_RATE;
  p.kpFilterTau = KP_FILTER_TAU;
  p.maxAngle = MAX_ANGLE;
  p.rearmAngle = REARM_ANGLE;
  p.mpuDlpfMode = MPU_DLPF_MODE;
  p.useGyroDerivative = USE_GYRO_DERIVATIVE;
  return p;
}

static void commit(const Params &p)
{
  portENTER_CRITICAL(&paramsMux);
  active = p;
  version++;
  portEXIT_CRITICAL(&paramsMux);
}

void paramsInit()
{
  Params p = paramsFactory();
  fromNvs = false;

  Preferences prefs;
  prefs.begin("params", true);
  if (prefs.getUShort("layout", 0) == PARAMS_LAYOUT_VERSION &&
      prefs.getBytesLength("blob") == sizeof(Params))
  {
    Params stored;
    prefs.getBytes("blob", &stored, sizeof(Params));
    String err;
    if (validate(stored, err))
    {
      p = stored;
      fromNvs = true;
    }
  }
  prefs.end();

  commit(p);
}

bool paramsLoadedFromNvs()
{
  return fromNvs;
}

uint32_t paramsSnapshot(Params &out)
{
  portENTER_CRITICAL(&paramsMux);
  out = active;
  uint32_t v = version;
  portEXIT_CRITICAL(&paramsMux);
  return v;
}

bool paramsApplyJson(JsonObjectConst in, String &err)
{
  Params cand;
  paramsSnapshot(cand);

  for (JsonPairConst kv : in)
  {
    const ParamDesc *d = findDesc(kv.key().c_str());
    if (!d)
    {
      err = String("parámetro desconocido: ") + kv.key().c_str();
      return false;
    }
    JsonVariantConst v = kv.value();
    switch (d->type)
    {
    case ParamType::Structure:
    {
      ControlStructure s;
      if (!v.is<const char *>() || !parseStructure(v.as<const char *>(), s))
      {
        err = String(d->key) + " debe ser \"SpeedOuter\" o \"AngleOuter\"";
        return false;
      }
      writeField(cand, *d, (float)(int)s);
      break;
    }
    case ParamType::Bool:
      if (!v.is<bool>())
      {
        err = String(d->key) + " debe ser true o false";
        return false;
      }
      writeField(cand, *d, v.as<bool>() ? 1 : 0);
      break;
    case ParamType::Int:
      if (!v.is<int>())
      {
        err = String(d->key) + " debe ser entero";
        return false;
      }
      {
        int iv = v.as<int>();
        if (iv < d->min || iv > d->max)
        {
          err = String(d->key) + " fuera de rango [" + String((int)d->min) + ", " + String((int)d->max) + "]";
          return false;
        }
        writeField(cand, *d, iv);
      }
      break;
    default:
      if (!v.is<float>())
      {
        err = String(d->key) + " debe ser numérico";
        return false;
      }
      writeField(cand, *d, v.as<float>());
    }
  }

  if (!validate(cand, err))
    return false;
  commit(cand);
  return true;
}

void paramsRestoreFactory()
{
  commit(paramsFactory());
}

bool paramsSave()
{
  Params p;
  paramsSnapshot(p);
  Preferences prefs;
  if (!prefs.begin("params", false))
    return false;
  bool ok = prefs.putBytes("blob", &p, sizeof(Params)) == sizeof(Params);
  ok = ok && prefs.putUShort("layout", PARAMS_LAYOUT_VERSION) == sizeof(uint16_t);
  prefs.end();
  if (ok)
    fromNvs = true;
  return ok;
}

static void putValue(JsonObject out, const Params &p, const ParamDesc &d)
{
  float v = readField(p, d);
  switch (d.type)
  {
  case ParamType::Structure:
    out[d.key] = structureName((ControlStructure)(int)v);
    break;
  case ParamType::Bool:
    out[d.key] = v != 0;
    break;
  case ParamType::Int:
    out[d.key] = (int)v;
    break;
  default:
    out[d.key] = v;
  }
}

void paramsToJson(const Params &p, JsonObject out)
{
  for (size_t i = 0; i < N_DESCS; i++)
    putValue(out, p, DESCS[i]);
}

void paramsSchemaToJson(JsonArray out)
{
  static const char *TYPE_NAMES[] = {"float", "int", "bool", "enum", "sign"};
  Params def = paramsFactory();
  for (size_t i = 0; i < N_DESCS; i++)
  {
    const ParamDesc &d = DESCS[i];
    JsonObject o = out.add<JsonObject>();
    o["key"] = d.key;
    o["type"] = TYPE_NAMES[(int)d.type];
    if (d.type == ParamType::Structure)
    {
      JsonArray opts = o["options"].to<JsonArray>();
      opts.add("SpeedOuter");
      opts.add("AngleOuter");
    }
    else
    {
      o["min"] = d.min;
      o["max"] = d.max;
    }
    JsonDocument tmp; // valor de fábrica con el mismo tipo JSON que en "params"
    putValue(tmp.to<JsonObject>(), def, d);
    o["def"] = tmp[d.key];
    o["unit"] = d.unit;
    o["desc"] = d.desc;
  }
}
