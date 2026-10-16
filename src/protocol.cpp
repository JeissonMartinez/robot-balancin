#include "protocol.h"
#include "bench.h"
#include "rpm_cascade.h"
#include "config.h"
#include "motor_test.h"
#include "mpu_block.h"
#include "params.h"
#include "console.h"
#include "wifi_link.h"
#include "mqtt_link.h"
#include <ArduinoJson.h>

static void send(JsonDocument &doc, Print &out)
{
  serializeJson(doc, out);
  out.println();
}

// ---------------------------------------------------------------------------------
// Acciones bloqueantes (también las usan el botón y las teclas en main.cpp)
// ---------------------------------------------------------------------------------
static void calibrateAction()
{
  Console.println(">> Iniciando calibración MPU (robot quieto y vertical)...");
  calibrateMPU();
  Console.println(">> Calibración MPU completa.");
}

bool runCalibration()
{
  return !benchActive() && runWithControlPaused(calibrateAction);
}

bool runDeadband()
{
  return !benchActive() && runWithControlPaused(runDeadbandTest);
}

static bool saveResult = false;
static void saveAction()
{
  saveResult = paramsSave();
}

// ---------------------------------------------------------------------------------
// Mensajes
// ---------------------------------------------------------------------------------
void protocolHello(Print &out)
{
  Params p;
  paramsSnapshot(p);
  JsonDocument doc;
  doc["type"] = "hello";
  doc["fw"] = FW_VERSION;
  doc["proto"] = PROTOCOL_VERSION;
  doc["period_ms"] = CONTROL_PERIOD_MS;
  doc["structure"] = structureName(p.structure);
  doc["params_src"] = paramsLoadedFromNvs() ? "nvs" : "factory";
  JsonObject enc = doc["enc"].to<JsonObject>();
  enc["ppr"] = ENC_PPR_MOTOR;
  enc["decode"] = ENC_DECODE;
  enc["gear"] = GEAR_RATIO;
  enc["cpw"] = PPR_WHEEL;
  enc["single_l"] = ENC_L_SINGLE_CHANNEL;
  doc["bench"] = true;
  send(doc, out);
}

void protocolWriteParams(Print &out)
{
  Params p;
  paramsSnapshot(p);
  JsonDocument doc;
  doc["type"] = "params";
  paramsToJson(p, doc["params"].to<JsonObject>());
  send(doc, out);
}

void protocolWriteTelemetry(const Telemetry &t, Print &out, bool json)
{
  char buf[520]; // peor caso ≈ 430 bytes
  int n;
  if (json)
    n = snprintf(buf, sizeof(buf),
                 "{\"type\":\"tel\",\"seq\":%lu,\"t\":%lu,\"ang\":%.2f,\"ref\":%.2f,\"w\":%.1f,"
                 "\"pwm\":%.1f,\"pwmM\":%.1f,\"rpmL\":%.1f,\"rpmR\":%.1f,\"kp\":%.2f,\"dt\":%.4f,"
                 "\"uP\":%.1f,\"uI\":%.1f,\"uD\":%.1f,\"rr\":%.2f,\"acc\":%.1f,\"ym\":%.2f,\"uF\":%.1f,"
                 "\"x\":%.2f,\"xr\":%.2f,\"vr\":%.1f,\"psi\":%.1f,\"psir\":%.1f,\"wz\":%.1f,\"dl\":%.1f,"
                 "\"st\":\"%s\"}\n",
                 (unsigned long)t.seq, (unsigned long)t.tMs, t.angle, t.angleRef, t.rate, t.pwm, t.pwmMotor,
                 t.rpmL, t.rpmR, t.kp, t.dt, t.uP, t.uI, t.uD, t.rpmRef, t.accel, t.rpmMeas, t.uF, t.xCm, t.xRefCm, t.vRef, t.psi, t.psiRef,
                 t.yawRate, t.dL, robotStateName(t.state));
  else
    n = snprintf(buf, sizeof(buf),
                 "Ang: %.2f | Ref: %.2f | w: %.1f | PWM: %.1f | RPM L: %.1f | RPM R: %.1f | Kp: %.2f | dt: %.4f | %s\n",
                 t.angle, t.angleRef, t.rate, t.pwm, t.rpmL, t.rpmR, t.kp, t.dt,
                 t.state == RobotState::Active ? "ACTIVO" : (t.state == RobotState::EStop ? "PARADA DE EMERGENCIA" : "MOTORES OFF"));
  if (n > 0)
    out.write((const uint8_t *)buf, min(n, (int)sizeof(buf) - 1));
}

void protocolWriteBench(const BenchFrame &f, Print &out, bool json)
{
  char buf[460];
  int n;
  if (json)
    n = snprintf(buf, sizeof(buf),
                 "{\"type\":\"mot\",\"seq\":%lu,\"t\":%lu,\"pwmL\":%d,\"pwmR\":%d,\"cL\":%ld,\"cR\":%ld,"
                 "\"rpmL\":%.2f,\"rpmR\":%.2f,\"eLA\":%lu,\"eLB\":%lu,\"eRA\":%lu,\"eRB\":%lu,\"lv\":%u,\"es\":%d,"
                 "\"mode\":\"%s\",\"ref\":%.2f,\"rf\":%.2f,\"y\":%.2f,\"uF\":%.1f,\"uP\":%.1f,\"uI\":%.1f,"
                 "\"cs\":\"%s\",\"ci\":%u,\"ck\":%lu,\"cn\":%lu}\n",
                 (unsigned long)f.seq, (unsigned long)f.tMs, f.pwmL, f.pwmR, (long)f.cL, (long)f.cR, f.rpmL, f.rpmR,
                 (unsigned long)f.edges[0], (unsigned long)f.edges[1], (unsigned long)f.edges[2],
                 (unsigned long)f.edges[3], f.levels, f.estop ? 1 : 0, f.rpmMode ? "rpm" : "pwm", f.ref, f.refF, f.meas, f.uF,
                 f.uP, f.uI, benchCapStateName(f.capState), f.capId, (unsigned long)f.capCount,
                 (unsigned long)f.capTotal);
  else if (f.rpmMode)
    n = snprintf(buf, sizeof(buf),
                 "Banco RPM | ref %.1f | medida %.1f | PWM %d (ff %.1f P %.1f I %.1f) | RPM L %.1f R %.1f\n", f.ref,
                 f.meas, f.pwmR, f.uF, f.uP, f.uI, f.rpmL, f.rpmR);
  else
    n = snprintf(buf, sizeof(buf),
                 "Banco | PWM L %d R %d | RPM L %.1f R %.1f | cuentas L %ld R %ld | flancos A/B L %lu/%lu R %lu/%lu | AB L %d%d R %d%d\n",
                 f.pwmL, f.pwmR, f.rpmL, f.rpmR, (long)f.cL, (long)f.cR, (unsigned long)f.edges[0],
                 (unsigned long)f.edges[1], (unsigned long)f.edges[2], (unsigned long)f.edges[3], f.levels & 1,
                 (f.levels >> 1) & 1, (f.levels >> 2) & 1, (f.levels >> 3) & 1);
  if (n > 0)
    out.write((const uint8_t *)buf, min(n, (int)sizeof(buf) - 1));
}

// ---------------------------------------------------------------------------------
// Comandos
// ---------------------------------------------------------------------------------
static void ack(Print &out, JsonVariantConst id, bool ok, const String &err = String())
{
  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = ok;
  if (!ok)
    doc["err"] = err;
  send(doc, out);
}

// ack con los parámetros activos, para que la HMI se sincronice tras set/defaults/get
static void ackParams(Print &out, JsonVariantConst id)
{
  Params p;
  paramsSnapshot(p);
  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = true;
  paramsToJson(p, doc["params"].to<JsonObject>());
  send(doc, out);
}

static void handleTel(JsonDocument &in, Channel &ch, JsonVariantConst id)
{
  Print &out = ch.out;
  TelemetryOutput next = ch.tel;
  if (!in["on"].isNull())
  {
    if (!in["on"].is<bool>())
      return ack(out, id, false, "on debe ser true o false");
    next.enabled = in["on"].as<bool>();
  }
  if (!in["fmt"].isNull())
  {
    const char *fmt = in["fmt"] | "";
    if (strcmp(fmt, "json") == 0)
      next.json = true;
    else if (strcmp(fmt, "text") == 0)
      next.json = false;
    else
      return ack(out, id, false, "fmt debe ser \"json\" o \"text\"");
  }
  if (!in["div"].isNull())
  {
    int div = in["div"] | 0;
    if (!in["div"].is<int>() || div < 1 || div > 50)
      return ack(out, id, false, "div debe ser entero en [1, 50]");
    next.div = div;
  }
  ch.tel = next;

  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = true;
  doc["on"] = ch.tel.enabled;
  doc["fmt"] = ch.tel.json ? "json" : "text";
  doc["div"] = ch.tel.div;
  doc["dropped"] = droppedTelemetry();
  send(doc, out);
}

static void handleWifi(JsonDocument &in, Print &out, JsonVariantConst id)
{
  if (!in["set"].isNull())
  {
    if (!in["set"].is<JsonObjectConst>())
      return ack(out, id, false, "set debe ser un objeto");
    String err;
    if (!wifiConfigure(in["set"].as<JsonObjectConst>(), err))
      return ack(out, id, false, err);
  }
  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = true;
  wifiStatusJson(doc["wifi"].to<JsonObject>());
  send(doc, out);
}

static void handleMqtt(JsonDocument &in, Print &out, JsonVariantConst id)
{
  if (!in["set"].isNull())
  {
    if (!in["set"].is<JsonObjectConst>())
      return ack(out, id, false, "set debe ser un objeto");
    String err;
    if (!mqttConfigure(in["set"].as<JsonObjectConst>(), err))
      return ack(out, id, false, err);
  }
  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = true;
  mqttStatusJson(doc["mqtt"].to<JsonObject>());
  send(doc, out);
}

static void ackBench(Print &out, JsonVariantConst id)
{
  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = true;
  benchStatusJson(doc["bench"].to<JsonObject>());
  send(doc, out);
}

// Entero opcional: `def` si falta; false si está pero no es entero
static bool optInt(JsonDocument &in, const char *key, int def, int &out)
{
  if (in[key].isNull())
  {
    out = def;
    return true;
  }
  if (!in[key].is<int>())
    return false;
  out = in[key].as<int>();
  return true;
}

static void handleBench(JsonDocument &in, Print &out, JsonVariantConst id)
{
  const char *op = in["op"] | "status";
  String err;
  if (strcmp(op, "status") == 0)
    return ackBench(out, id);
  if (strcmp(op, "on") == 0)
    return benchEnter(err) ? ackBench(out, id) : ack(out, id, false, err);
  if (strcmp(op, "off") == 0)
  {
    benchExit();
    return ackBench(out, id);
  }
  if (strcmp(op, "stop") == 0)
  {
    benchStop();
    return ackBench(out, id);
  }
  if (strcmp(op, "pwm") == 0)
  {
    JsonDocument st;
    benchStatusJson(st.to<JsonObject>());
    int l, r;
    if (!optInt(in, "l", st["pwm"][0] | 0, l) || !optInt(in, "r", st["pwm"][1] | 0, r))
      return ack(out, id, false, "l y r deben ser enteros");
    return benchSetPwm(l, r, err) ? ackBench(out, id) : ack(out, id, false, err);
  }
  if (strcmp(op, "rpm") == 0)
  {
    if (!in["r"].is<float>() && !in["r"].is<int>())
      return ack(out, id, false, "r (RPM) debe ser un número");
    return benchSetRpm(in["r"].as<float>(), err) ? ackBench(out, id) : ack(out, id, false, err);
  }
  if (strcmp(op, "rpmcfg") == 0)
  {
    // Campos opcionales: los que faltan se mantienen
    RpmConfig c = benchRpmConfig();
    auto num = [&](const char *k, float &dst) -> bool {
      if (in[k].isNull())
        return true;
      if (!in[k].is<float>() && !in[k].is<int>())
        return false;
      dst = in[k].as<float>();
      return true;
    };
    auto u8 = [&](const char *k, uint8_t &dst) -> bool {
      if (in[k].isNull())
        return true;
      if (!in[k].is<int>() || in[k].as<int>() < 0 || in[k].as<int>() > 255)
        return false;
      dst = in[k].as<int>();
      return true;
    };
    auto flag = [&](const char *k, bool &dst) -> bool {
      if (in[k].isNull())
        return true;
      if (!in[k].is<bool>())
        return false;
      dst = in[k].as<bool>();
      return true;
    };
    if (!num("kp", c.kp) || !num("ki", c.ki) || !u8("ts_ms", c.tsMs) || !u8("win_ms", c.winMs) || !flag("ff", c.ff) ||
        !num("ff_k", c.ffK) || !num("ff_b", c.ffB) || !num("i_max", c.iMax) || !flag("follow", c.follow) || !u8("ref_tau_ms", c.refTauMs))
      return ack(out, id, false, "rpmcfg: kp, ki, ff_k, ff_b, i_max numéricos; ts_ms, win_ms, ref_tau_ms enteros; ff, follow true/false");
    return benchSetRpmConfig(c, err) ? ackBench(out, id) : ack(out, id, false, err);
  }
  if (strcmp(op, "run") == 0)
  {
    const char *motor = in["motor"] | "";
    uint8_t mask = strcmp(motor, "L") == 0 ? 1 : strcmp(motor, "R") == 0 ? 2 : strcmp(motor, "LR") == 0 ? 3 : 0;
    const char *kind = in["kind"] | "pwm";
    const bool rpm = strcmp(kind, "rpm") == 0;
    if (!rpm && strcmp(kind, "pwm") != 0)
      return ack(out, id, false, "kind debe ser \"pwm\" o \"rpm\"");
    if (!in["seq"].is<JsonArrayConst>())
      return ack(out, id, false, "seq debe ser una lista de [valor, ms]");
    JsonArrayConst seq = in["seq"].as<JsonArrayConst>();
    if (seq.size() > BENCH_MAX_SEGMENTS)
      return ack(out, id, false, String("seq admite hasta ") + BENCH_MAX_SEGMENTS + " tramos");
    float val[BENCH_MAX_SEGMENTS];
    uint32_t ms[BENCH_MAX_SEGMENTS];
    uint8_t n = 0;
    for (JsonVariantConst s : seq)
    {
      // PWM: enteros; RPM: admite decimales
      const bool valOk = rpm ? (s[0].is<float>() || s[0].is<int>()) : s[0].is<int>();
      if (!s.is<JsonArrayConst>() || s.size() != 2 || !valOk || !s[1].is<int>())
        return ack(out, id, false, rpm ? "cada tramo debe ser [rpm, ms] (ms entero)" : "cada tramo debe ser [pwm, ms] con enteros");
      const int d = s[1].as<int>();
      if (d < 1)
        return ack(out, id, false, "cada tramo debe durar al menos 1 ms");
      val[n] = s[0].as<float>();
      ms[n] = d;
      n++;
    }
    int period;
    if (!optInt(in, "period_ms", 2, period) || period < 1)
      return ack(out, id, false, "period_ms debe ser entero positivo");
    return benchRun(mask, rpm, val, ms, n, period, err) ? ackBench(out, id) : ack(out, id, false, err);
  }
  if (strcmp(op, "get") == 0)
  {
    int from, count;
    if (!optInt(in, "from", 0, from) || !optInt(in, "count", 100, count) || from < 0 || count < 1 ||
        count > BENCH_GET_MAX_ROWS)
      return ack(out, id, false, String("from ≥ 0 y count en [1, ") + BENCH_GET_MAX_ROWS + "]");
    const uint32_t total = benchReadableSamples();
    if (!total)
      return ack(out, id, false, "no hay un ensayo terminado para leer");
    // A mano: hasta 200 filas no conviene armarlas en un JsonDocument
    JsonDocument st;
    benchStatusJson(st.to<JsonObject>());
    out.print("{\"type\":\"ack\",\"id\":");
    serializeJson(id, out);
    out.printf(",\"ok\":true,\"cap\":%u,\"from\":%d,\"total\":%lu,\"rows\":[", (unsigned)(st["cap"]["id"] | 0),
               from, (unsigned long)total);
    benchWriteRows(out, from, count);
    out.print("]}\n");
    return;
  }
  ack(out, id, false, String("op desconocida: ") + op);
}

void protocolHandleLine(const char *line, Channel &ch)
{
  Print &out = ch.out;
  JsonDocument in;
  DeserializationError e = deserializeJson(in, line);
  if (e)
    return ack(out, JsonVariantConst(), false, String("JSON inválido: ") + e.c_str());

  JsonVariantConst id = in["id"];
  const char *type = in["type"] | "";
  const char *cmd = in["cmd"] | "";
  if (strcmp(type, "cmd") != 0)
    return ack(out, id, false, "type debe ser \"cmd\"");

  if (strcmp(cmd, "hello") == 0)
  {
    protocolHello(out);
    return ack(out, id, true);
  }
  if (strcmp(cmd, "get") == 0)
    return ackParams(out, id);
  if (strcmp(cmd, "schema") == 0)
  {
    JsonDocument doc;
    doc["type"] = "ack";
    doc["id"] = id;
    doc["ok"] = true;
    paramsSchemaToJson(doc["schema"].to<JsonArray>());
    return send(doc, out);
  }
  if (strcmp(cmd, "set") == 0)
  {
    if (!in["params"].is<JsonObjectConst>())
      return ack(out, id, false, "set requiere un objeto params");
    String err;
    if (!paramsApplyJson(in["params"].as<JsonObjectConst>(), err))
      return ack(out, id, false, err);
    return ackParams(out, id);
  }
  if (strcmp(cmd, "defaults") == 0)
  {
    paramsRestoreFactory();
    return ackParams(out, id);
  }
  if (strcmp(cmd, "save") == 0)
  {
    saveResult = false;
    if (!runWithControlPaused(saveAction))
      return ack(out, id, false, "la tarea de control no se detuvo");
    return saveResult ? ack(out, id, true) : ack(out, id, false, "no se pudo escribir en NVS");
  }
  if ((strcmp(cmd, "calib") == 0 || strcmp(cmd, "deadband") == 0) && benchActive())
    return ack(out, id, false, "el banco de motores está activo: salir del banco primero");
  if (strcmp(cmd, "calib") == 0)
    return runCalibration() ? ack(out, id, true) : ack(out, id, false, "la tarea de control no se detuvo");
  if (strcmp(cmd, "deadband") == 0)
    return runDeadband() ? ack(out, id, true) : ack(out, id, false, "la tarea de control no se detuvo");
  if (strcmp(cmd, "estop") == 0)
  {
    setEStop(true);
    return ack(out, id, true);
  }
  if (strcmp(cmd, "arm") == 0)
  {
    setEStop(false);
    return ack(out, id, true);
  }
  if (strcmp(cmd, "tel") == 0)
    return handleTel(in, ch, id);
  if (strcmp(cmd, "wifi") == 0)
    return handleWifi(in, out, id);
  if (strcmp(cmd, "mqtt") == 0)
    return handleMqtt(in, out, id);
  if (strcmp(cmd, "bench") == 0)
    return handleBench(in, out, id);
  if (strcmp(cmd, "drive") == 0)
  {
    // Conducción (M4, RpmCascade): {"v": RPM, "r": °/s} o {"home": true}. Se vence a los
    // DRIVE_TIMEOUT_MS: el cliente lo renueva mientras se mantenga pulsado.
    if (in["home"] | false)
      driveHome();
    else
    {
      const bool vOk = in["v"].isNull() || in["v"].is<float>() || in["v"].is<int>();
      const bool rOk = in["r"].isNull() || in["r"].is<float>() || in["r"].is<int>();
      if (!vOk || !rOk)
        return ack(out, id, false, "v (RPM) y r (°/s) deben ser números");
      driveCommand(in["v"] | 0.0f, in["r"] | 0.0f);
    }
    float v, r;
    bool home;
    driveStatus(v, r, home);
    JsonDocument doc;
    doc["type"] = "ack";
    doc["id"] = id;
    doc["ok"] = true;
    JsonObject d = doc["drive"].to<JsonObject>();
    d["v"] = v;
    d["r"] = r;
    d["home"] = home;
    return send(doc, out);
  }

  ack(out, id, false, String("comando desconocido: ") + cmd);
}
