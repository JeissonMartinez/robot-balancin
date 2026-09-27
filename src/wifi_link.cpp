#include "wifi_link.h"
#include "config.h"
#include "console.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

enum class Mode : uint8_t
{
  Off = 0,
  AP = 1,
  STA = 2
};

// Lo que está funcionando ahora (puede diferir del modo configurado: respaldo AP)
enum class Active : uint8_t
{
  Off,
  AP,
  StaConnecting,
  Sta,
  ApFallback
};

struct WifiConfig
{
  Mode mode;
  String ssid;
  String pass;
  String apPass; // vacío = clave por defecto del robot
};

namespace
{
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

WifiConfig cfg;
Active active = Active::Off;
uint32_t staStartMs = 0;
bool staAnnounced = false;
uint32_t applyAtMs = 0; // reconfiguración pendiente (0 = ninguna)
uint32_t lastCleanupMs = 0;
bool serverStarted = false;
String apSsid, apPassDefault, hostname;

// Mensajes recibidos por WebSocket, atendidos en loop(). line == nullptr: cliente nuevo
struct RxItem
{
  uint32_t client;
  char *line;
};
QueueHandle_t rxQueue = nullptr;
const size_t MAX_LINE = 1023;

/**
 * Print que junta una línea completa y la envía como UN mensaje WebSocket (a un cliente o
 * a todos). La línea no se puede partir: el receptor separa mensajes por trama, y un JSON
 * cortado en dos no se entiende (la respuesta a "schema" ocupa ~3.5 kB).
 */
class WsPrint : public Print
{
public:
  static const size_t MAX_MSG = 16384; // tope de seguridad; ningún mensaje del protocolo llega
  uint32_t target = 0;                 // 0 = todos
  WsPrint() { line.reserve(1024); }
  size_t write(uint8_t c) override
  {
    if (c == '\n')
    {
      flush();
      return 1;
    }
    if (c != '\r' && line.length() < MAX_MSG)
      line += (char)c;
    return 1;
  }
  size_t write(const uint8_t *data, size_t n) override
  {
    for (size_t i = 0; i < n; i++)
      write(data[i]);
    return n;
  }
  void flush() override
  {
    if (!line.length())
      return;
    if (ws.count())
    {
      if (target)
        ws.text(target, line.c_str(), line.length());
      else
        ws.textAll(line.c_str(), line.length());
    }
    line = "";
  }

private:
  String line;
};

WsPrint wsOut;  // respuestas y telemetría
WsPrint wsLog;  // espejo de la consola (siempre a todos)
Channel channel = {wsOut, {true, true, 1}}; // por WiFi: JSON a 50 Hz

bool rxPending()
{
  return rxQueue && uxQueueMessagesWaiting(rxQueue) > 0;
}

const char *modeName(Mode m)
{
  return m == Mode::AP ? "ap" : m == Mode::STA ? "sta" : "off";
}

const char *activeName(Active a)
{
  switch (a)
  {
  case Active::AP:
    return "ap";
  case Active::StaConnecting:
    return "sta_connecting";
  case Active::Sta:
    return "sta";
  case Active::ApFallback:
    return "ap_fallback";
  default:
    return "off";
  }
}

const String &apPassword()
{
  return cfg.apPass.length() ? cfg.apPass : apPassDefault;
}

// ------------------------------------------------------------------ NVS
void loadConfig()
{
  // Lectura-escritura: en sólo lectura, la primera vez (namespace inexistente) el core
  // registra un error "nvs_open failed: NOT_FOUND" aunque no pase nada
  Preferences p;
  p.begin("wifi", false);
  cfg.mode = (Mode)p.getUChar("mode", (uint8_t)Mode::AP);
  // isKey evita el error "nvs_get_str ... NOT_FOUND" mientras no se haya guardado nada
  cfg.ssid = p.isKey("ssid") ? p.getString("ssid") : "";
  cfg.pass = p.isKey("pass") ? p.getString("pass") : "";
  cfg.apPass = p.isKey("ap_pass") ? p.getString("ap_pass") : "";
  p.end();
  if ((uint8_t)cfg.mode > 2)
    cfg.mode = Mode::AP;
}

bool saveConfig()
{
  Preferences p;
  if (!p.begin("wifi", false))
    return false;
  p.putUChar("mode", (uint8_t)cfg.mode);
  p.putString("ssid", cfg.ssid);
  p.putString("pass", cfg.pass);
  p.putString("ap_pass", cfg.apPass);
  p.end();
  return true;
}

// ------------------------------------------------------------------ radio
void startAP(bool fallback)
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPassword().c_str(), 1, 0, 4);
  active = fallback ? Active::ApFallback : Active::AP;
}

// El servidor necesita la pila TCP/IP (lwIP), que en Arduino-ESP32 2.x se crea al
// encender la radio: arrancarlo antes aborta el programa (assert en tcpip_api_call).
// Por eso se arranca aquí, después de WiFi.mode(), y una sola vez.
void startServer()
{
  if (serverStarted)
    return;
  server.begin();
  serverStarted = true;
}

void startMdns()
{
  MDNS.end();
  if (MDNS.begin(hostname.c_str()))
    MDNS.addService("http", "tcp", 80);
}

void applyConfig()
{
  ws.closeAll();
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  staAnnounced = false;
  switch (cfg.mode)
  {
  case Mode::Off:
    WiFi.mode(WIFI_OFF);
    active = Active::Off;
    Console.println(">> WiFi apagado.");
    return;
  case Mode::STA:
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname.c_str());
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    active = Active::StaConnecting;
    staStartMs = millis();
    Console.printf(">> WiFi: conectando a «%s»...\n", cfg.ssid.c_str());
    break;
  case Mode::AP:
    startAP(false);
    wifiPrintInfo(Console);
    break;
  }
  WiFi.setSleep(false); // menor latencia
  startServer();
  startMdns();
}

// ------------------------------------------------------------------ WebSocket
void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  // Corre en la tarea de AsyncTCP (núcleo 0): sólo se encola, nunca se procesa aquí
  if (type == WS_EVT_CONNECT)
  {
    if (ws.count() > WIFI_MAX_WS_CLIENTS)
    {
      client->close();
      return;
    }
    RxItem it = {client->id(), nullptr};
    xQueueSend(rxQueue, &it, 0);
    return;
  }
  if (type != WS_EVT_DATA)
    return;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  // Sólo mensajes de texto completos en una trama (los del gateway son cortos)
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT || len > MAX_LINE)
    return;
  char *line = (char *)malloc(len + 1);
  if (!line)
    return;
  memcpy(line, data, len);
  while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    len--;
  line[len] = '\0';
  RxItem it = {client->id(), line};
  if (xQueueSend(rxQueue, &it, 0) != pdTRUE)
    free(line);
}
} // namespace

// ------------------------------------------------------------------ API
void wifiInit()
{
  uint64_t mac = ESP.getEfuseMac();
  uint8_t b[6];
  for (int i = 0; i < 6; i++)
    b[i] = (mac >> (8 * i)) & 0xFF;
  char s[32];
  snprintf(s, sizeof(s), "Balancin-%02X%02X", b[4], b[5]);
  apSsid = s;
  snprintf(s, sizeof(s), "balancin-%02x%02x", b[4], b[5]);
  hostname = s;
  snprintf(s, sizeof(s), "bal-%02x%02x%02x", b[3], b[4], b[5]);
  apPassDefault = s;

  rxQueue = xQueueCreate(16, sizeof(RxItem));
  loadConfig();
  WiFi.persistent(false); // la configuración la guarda este módulo, no el core

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["robot"] = "balancin";
    doc["fw"] = FW_VERSION;
    doc["proto"] = PROTOCOL_VERSION;
    doc["ws"] = "/ws";
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
  });

  consoleSetMirror(&wsLog, rxPending);
  applyConfig();
}

void wifiLoop()
{
  uint32_t now = millis();

  if (applyAtMs && (int32_t)(now - applyAtMs) >= 0)
  {
    applyAtMs = 0;
    applyConfig();
  }

  if (active == Active::StaConnecting)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      active = Active::Sta;
      wifiPrintInfo(Console);
    }
    else if (now - staStartMs > WIFI_STA_TIMEOUT_MS)
    {
      Console.printf(">> WiFi: no se pudo conectar a «%s». Se levanta la red propia.\n", cfg.ssid.c_str());
      WiFi.disconnect(true);
      startAP(true);
      startServer();
      startMdns();
      wifiPrintInfo(Console);
    }
  }
  else if (active == Active::Sta && WiFi.status() != WL_CONNECTED)
  {
    // Se perdió la red: el core reintenta solo; se informa una vez
    if (!staAnnounced)
      Console.println(">> WiFi: se perdió la red, reintentando...");
    staAnnounced = true;
    active = Active::StaConnecting;
    staStartMs = now;
    WiFi.reconnect();
  }

  // Comandos recibidos: se atienden aquí, en el mismo hilo que los del Serial
  RxItem it;
  while (rxQueue && xQueueReceive(rxQueue, &it, 0) == pdTRUE)
  {
    wsOut.flush();
    wsOut.target = it.client;
    if (it.line)
    {
      if (it.line[0] == '{')
        protocolHandleLine(it.line, channel);
      free(it.line);
    }
    else
      protocolHello(wsOut); // saludo sólo al cliente nuevo
    wsOut.flush();
    wsOut.target = 0;
  }

  if (now - lastCleanupMs > 1000)
  {
    lastCleanupMs = now;
    ws.cleanupClients(WIFI_MAX_WS_CLIENTS);
  }
}

Channel &wifiChannel()
{
  return channel;
}

bool wifiHasClients()
{
  return ws.count() > 0;
}

bool wifiCanSend()
{
  return ws.availableForWriteAll();
}

void wifiStatusJson(JsonObject out)
{
  out["mode"] = modeName(cfg.mode);
  out["active"] = activeName(active);
  out["hostname"] = String(hostname) + ".local";
  out["ap_ssid"] = apSsid;
  out["ap_pass"] = apPassword();
  out["ap_pass_default"] = cfg.apPass.length() == 0;
  out["ssid"] = cfg.ssid;
  out["pass_set"] = cfg.pass.length() > 0;
  bool apOn = active == Active::AP || active == Active::ApFallback;
  if (apOn)
    out["ip"] = WiFi.softAPIP().toString();
  else if (active == Active::Sta)
  {
    out["ip"] = WiFi.localIP().toString();
    out["rssi"] = WiFi.RSSI();
  }
  out["clients"] = ws.count();
}

bool wifiConfigure(JsonObjectConst in, String &err)
{
  WifiConfig next = cfg;
  if (!in["mode"].isNull())
  {
    const char *m = in["mode"] | "";
    if (strcmp(m, "ap") == 0)
      next.mode = Mode::AP;
    else if (strcmp(m, "sta") == 0)
      next.mode = Mode::STA;
    else if (strcmp(m, "off") == 0)
      next.mode = Mode::Off;
    else
    {
      err = "mode debe ser \"ap\", \"sta\" u \"off\"";
      return false;
    }
  }
  if (!in["ssid"].isNull())
    next.ssid = (const char *)(in["ssid"] | "");
  if (!in["pass"].isNull())
    next.pass = (const char *)(in["pass"] | "");
  if (!in["ap_pass"].isNull())
    next.apPass = (const char *)(in["ap_pass"] | "");

  if (next.mode == Mode::STA && (next.ssid.length() < 1 || next.ssid.length() > 32))
  {
    err = "ssid: entre 1 y 32 caracteres";
    return false;
  }
  if (next.pass.length() && (next.pass.length() < 8 || next.pass.length() > 63))
  {
    err = "pass: vacía o entre 8 y 63 caracteres";
    return false;
  }
  if (next.apPass.length() && (next.apPass.length() < 8 || next.apPass.length() > 63))
  {
    err = "ap_pass: vacía (clave del robot) o entre 8 y 63 caracteres";
    return false;
  }
  cfg = next;
  if (!saveConfig())
  {
    err = "no se pudo guardar en NVS";
    return false;
  }
  applyAtMs = millis() + 500; // deja salir la respuesta por la conexión actual
  if (!applyAtMs)
    applyAtMs = 1;
  return true;
}

void wifiPrintInfo(Print &out)
{
  switch (active)
  {
  case Active::AP:
  case Active::ApFallback:
    out.printf(">> WiFi: red «%s», clave «%s». HMI: gateway con WiFi → %s (ws://%s/ws).\n",
               apSsid.c_str(), apPassword().c_str(), WiFi.softAPIP().toString().c_str(),
               WiFi.softAPIP().toString().c_str());
    break;
  case Active::Sta:
    out.printf(">> WiFi: conectado a «%s», IP %s (%s.local), señal %d dBm.\n", cfg.ssid.c_str(),
               WiFi.localIP().toString().c_str(), hostname.c_str(), WiFi.RSSI());
    break;
  case Active::StaConnecting:
    out.printf(">> WiFi: conectando a «%s»...\n", cfg.ssid.c_str());
    break;
  default:
    out.println(">> WiFi apagado.");
  }
}
