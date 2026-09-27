/**
 * @file mqtt_link.h
 * @brief Canal MQTT del protocolo (F5, opcional; apagado por defecto).
 *
 * El robot se conecta a un broker (p. ej. Mosquitto en el PC del laboratorio) y usa:
 *  - balancin/<robot>/in      comandos hacia el robot (se suscribe)
 *  - balancin/<robot>/out     todo lo que el robot emite: ack, hello, logs, telemetría
 *  - balancin/<robot>/status  "online" / "offline" (retenido; "offline" es su testamento)
 * <robot> es el nombre de red del robot (wifiHostname(), p. ej. "balancin-b884").
 *
 * Cada mensaje es una línea del protocolo, como por Serial o WebSocket. La telemetría de
 * este canal arranca apagada; el gateway la activa con "tel" al conectarse. Se publica
 * desde una tarea propia (mqttTx, núcleo 0) con QoS 0, así loop() nunca espera a la red.
 * "hello" se
 * publica sólo en la primera conexión tras encender: así el gateway reconoce un reinicio
 * real y no lo confunde con una reconexión al broker.
 *
 * Configuración en NVS (namespace "mqtt"): enabled, host, port. Comando "mqtt" del protocolo.
 * El cliente (esp-mqtt del ESP-IDF) corre en su propia tarea; los comandos se encolan y se
 * atienden en loop(), igual que los del Serial y el WebSocket.
 */
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "protocol.h"

/** Carga la configuración. Llamar en setup() después de wifiInit(). */
void mqttInit();

/** Arranca o detiene el cliente según la red y la configuración; atiende los comandos. */
void mqttLoop();

/** Canal del protocolo por MQTT. */
Channel &mqttChannel();

/** @return true si está conectado al broker. */
bool mqttCanSend();

/** Estado: enabled, host, port, connected, topic. */
void mqttStatusJson(JsonObject out);

/** Valida y guarda {enabled, host, port}; se aplica en el siguiente mqttLoop(). */
bool mqttConfigure(JsonObjectConst in, String &err);
