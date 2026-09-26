#include "protocol.h"
#include "config.h"
#include "motor_test.h"
#include "mpu_block.h"
#include "params.h"
#include <ArduinoJson.h>

static TelemetryOutput output = {true, false, TELEMETRY_DIV_DEFAULT};

TelemetryOutput &telemetryOutput()
{
  return output;
}

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
  Serial.println(">> Iniciando calibración MPU (robot quieto y vertical)...");
  calibrateMPU();
  Serial.println(">> Calibración MPU completa.");
}

bool runCalibration()
{
  return runWithControlPaused(calibrateAction);
}

bool runDeadband()
{
  return runWithControlPaused(runDeadbandTest);
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
  char buf[320]; // peor caso ≈ 240 bytes
  int n;
  if (json)
    n = snprintf(buf, sizeof(buf),
                 "{\"type\":\"tel\",\"seq\":%lu,\"t\":%lu,\"ang\":%.2f,\"ref\":%.2f,\"w\":%.1f,"
                 "\"pwm\":%.1f,\"pwmM\":%.1f,\"rpmL\":%.1f,\"rpmR\":%.1f,\"kp\":%.2f,\"dt\":%.4f,"
                 "\"uP\":%.1f,\"uI\":%.1f,\"uD\":%.1f,\"st\":\"%s\"}\n",
                 (unsigned long)t.seq, (unsigned long)t.tMs, t.angle, t.angleRef, t.rate, t.pwm, t.pwmMotor,
                 t.rpmL, t.rpmR, t.kp, t.dt, t.uP, t.uI, t.uD, robotStateName(t.state));
  else
    n = snprintf(buf, sizeof(buf),
                 "Ang: %.2f | Ref: %.2f | w: %.1f | PWM: %.1f | RPM L: %.1f | RPM R: %.1f | Kp: %.2f | dt: %.4f | %s\n",
                 t.angle, t.angleRef, t.rate, t.pwm, t.rpmL, t.rpmR, t.kp, t.dt,
                 t.state == RobotState::Active ? "ACTIVO" : (t.state == RobotState::EStop ? "PARADA DE EMERGENCIA" : "MOTORES OFF"));
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

static void handleTel(JsonDocument &in, Print &out, JsonVariantConst id)
{
  TelemetryOutput next = output;
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
  output = next;

  JsonDocument doc;
  doc["type"] = "ack";
  doc["id"] = id;
  doc["ok"] = true;
  doc["on"] = output.enabled;
  doc["fmt"] = output.json ? "json" : "text";
  doc["div"] = output.div;
  doc["dropped"] = droppedTelemetry();
  send(doc, out);
}

void protocolHandleLine(const char *line, Print &out)
{
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
    return handleTel(in, out, id);

  ack(out, id, false, String("comando desconocido: ") + cmd);
}
