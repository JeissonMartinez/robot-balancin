#include "mqtt_link.h"
#include "console.h"
#include "wifi_link.h"
#include <Preferences.h>
#include <mqtt_client.h>

namespace
{
struct MqttConfig
{
  bool enabled;
  String host;
  uint16_t port;
};

MqttConfig cfg = {false, "", 1883};
esp_mqtt_client_handle_t client = nullptr;
volatile bool connected = false;
bool helloSent = false; // "hello" sólo en la primera conexión tras encender
bool restartPending = false;
String topicIn, topicOut, topicStatus, uri;

// Mensajes recibidos (line) o aviso de conexión (line == nullptr), atendidos en loop()
QueueHandle_t rxQueue = nullptr;
const size_t MAX_LINE = 1023;
const int OUTBOX_LIMIT = 16 * 1024; // bytes pendientes: por encima se descarta telemetría

/** Junta una línea y la publica en .../out como un mensaje (cola de esp-mqtt, no bloquea). */
class MqttPrint : public Print
{
public:
  MqttPrint() { line.reserve(512); }
  size_t write(uint8_t c) override
  {
    if (c == '\n')
    {
      flush();
      return 1;
    }
    if (c != '\r' && line.length() < 16384)
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
    if (client && connected)
      esp_mqtt_client_enqueue(client, topicOut.c_str(), line.c_str(), line.length(), 0, 0, true);
    line = "";
  }

private:
  String line;
};

MqttPrint out;
MqttPrint logOut;
Channel channel = {out, {false, true, 1}}; // telemetría apagada hasta que el gateway la pida

bool rxPending()
{
  return rxQueue && uxQueueMessagesWaiting(rxQueue) > 0;
}

void loadConfig()
{
  Preferences p;
  p.begin("mqtt", false);
  cfg.enabled = p.getBool("enabled", false);
  cfg.host = p.isKey("host") ? p.getString("host") : "";
  cfg.port = p.getUShort("port", 1883);
  p.end();
}

bool saveConfig()
{
  Preferences p;
  if (!p.begin("mqtt", false))
    return false;
  p.putBool("enabled", cfg.enabled);
  p.putString("host", cfg.host);
  p.putUShort("port", cfg.port);
  p.end();
  return true;
}

// Corre en la tarea de esp-mqtt: sólo se encola, nunca se procesa aquí
void onEvent(void *, esp_event_base_t, int32_t id, void *data)
{
  auto *e = (esp_mqtt_event_handle_t)data;
  switch ((esp_mqtt_event_id_t)id)
  {
  case MQTT_EVENT_CONNECTED:
  {
    connected = true;
    esp_mqtt_client_subscribe(e->client, topicIn.c_str(), 0);
    esp_mqtt_client_publish(e->client, topicStatus.c_str(), "online", 0, 1, 1);
    char *connectedMark = nullptr; // aviso a loop(): recién conectado
    xQueueSend(rxQueue, &connectedMark, 0);
    break;
  }
  case MQTT_EVENT_DISCONNECTED:
    connected = false;
    break;
  case MQTT_EVENT_DATA:
  {
    // Sólo mensajes completos del tema de comandos
    if (e->current_data_offset != 0 || e->data_len != e->total_data_len || e->data_len > (int)MAX_LINE)
      break;
    if (e->topic_len != (int)topicIn.length() || strncmp(e->topic, topicIn.c_str(), e->topic_len) != 0)
      break;
    char *line = (char *)malloc(e->data_len + 1);
    if (!line)
      break;
    memcpy(line, e->data, e->data_len);
    line[e->data_len] = '\0';
    if (xQueueSend(rxQueue, &line, 0) != pdTRUE)
      free(line);
    break;
  }
  default:
    break;
  }
}

void start()
{
  String base = "balancin/" + wifiHostname();
  topicIn = base + "/in";
  topicOut = base + "/out";
  topicStatus = base + "/status";
  uri = "mqtt://" + cfg.host + ":" + String(cfg.port);

  esp_mqtt_client_config_t c = {};
  c.uri = uri.c_str();
  c.client_id = wifiHostname().c_str();
  c.lwt_topic = topicStatus.c_str();
  c.lwt_msg = "offline";
  c.lwt_qos = 1;
  c.lwt_retain = 1;
  c.keepalive = 10;
  c.buffer_size = 2048;     // recepción (comandos)
  c.out_buffer_size = 6144; // envío: la respuesta a "schema" ocupa ~3.5 kB
  client = esp_mqtt_client_init(&c);
  if (!client)
  {
    Console.println(">> MQTT: no se pudo crear el cliente.");
    return;
  }
  esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onEvent, nullptr);
  esp_mqtt_client_start(client);
  Console.printf(">> MQTT: conectando a %s como %s...\n", uri.c_str(), wifiHostname().c_str());
}

void stop()
{
  if (!client)
    return;
  if (connected)
    esp_mqtt_client_publish(client, topicStatus.c_str(), "offline", 0, 1, 1);
  esp_mqtt_client_stop(client);
  esp_mqtt_client_destroy(client);
  client = nullptr;
  connected = false;
}
} // namespace

void mqttInit()
{
  rxQueue = xQueueCreate(16, sizeof(char *));
  loadConfig();
  consoleAddMirror(&logOut, rxPending);
}

void mqttLoop()
{
  if (restartPending)
  {
    restartPending = false;
    stop();
  }
  bool want = cfg.enabled && cfg.host.length() && wifiNetworkUp();
  if (want && !client)
    start();
  else if (!want && client)
  {
    stop();
    Console.println(">> MQTT desconectado.");
  }

  char *line;
  while (rxQueue && xQueueReceive(rxQueue, &line, 0) == pdTRUE)
  {
    out.flush();
    if (!line)
    {
      Console.printf(">> MQTT: conectado a %s (temas balancin/%s/...).\n", uri.c_str(), wifiHostname().c_str());
      if (!helloSent)
      {
        protocolHello(out);
        helloSent = true;
      }
    }
    else
    {
      if (line[0] == '{')
        protocolHandleLine(line, channel);
      free(line);
    }
    out.flush();
  }
}

Channel &mqttChannel()
{
  return channel;
}

bool mqttCanSend()
{
  return client && connected && esp_mqtt_client_get_outbox_size(client) < OUTBOX_LIMIT;
}

void mqttStatusJson(JsonObject o)
{
  o["enabled"] = cfg.enabled;
  o["host"] = cfg.host;
  o["port"] = cfg.port;
  o["connected"] = (bool)connected;
  o["topic"] = String("balancin/") + wifiHostname();
}

bool mqttConfigure(JsonObjectConst in, String &err)
{
  MqttConfig next = cfg;
  if (!in["enabled"].isNull())
  {
    if (!in["enabled"].is<bool>())
    {
      err = "enabled debe ser true o false";
      return false;
    }
    next.enabled = in["enabled"].as<bool>();
  }
  if (!in["host"].isNull())
    next.host = (const char *)(in["host"] | "");
  if (!in["port"].isNull())
  {
    int port = in["port"] | 0;
    if (!in["port"].is<int>() || port < 1 || port > 65535)
    {
      err = "port debe ser entero en [1, 65535]";
      return false;
    }
    next.port = port;
  }
  next.host.trim();
  if (next.enabled && (next.host.length() < 1 || next.host.length() > 63))
  {
    err = "host: IP o nombre del broker (1 a 63 caracteres)";
    return false;
  }
  cfg = next;
  if (!saveConfig())
  {
    err = "no se pudo guardar en NVS";
    return false;
  }
  restartPending = true;
  return true;
}
