/**
 * @file wifi_link.h
 * @brief WiFi del robot y servidor WebSocket para la HMI (F4).
 *
 * Modos (se guardan en NVS, namespace "wifi"):
 *  - ap  (por defecto): el robot crea su red "Balancin-XXXX" con clave única derivada del
 *        chip ("bal-xxxxxx"), IP 192.168.4.1.
 *  - sta: se une a una red existente. Si no conecta en WIFI_STA_TIMEOUT_MS, levanta su AP
 *        para no quedar inaccesible.
 *  - off: radio apagada.
 *
 * Servicios: WebSocket en ws://<ip>/ws (mismo protocolo que el Serial, un mensaje JSON
 * por trama), página de estado en http://<ip>/, y mDNS <hostname>.local.
 *
 * Hilos: el servidor corre en la tarea de AsyncTCP (núcleo 0). Los comandos recibidos se
 * encolan y se atienden en loop() (wifiLoop), igual que los del Serial, para que las
 * acciones que bloquean (calibrar, zona muerta, guardar) no frenen la red.
 */
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "protocol.h"

/** Carga la configuración, enciende la radio y arranca el servidor. Llamar en setup(). */
void wifiInit();

/** Atiende comandos recibidos, saludos a clientes nuevos, reconfiguración y limpieza. */
void wifiLoop();

/** Canal del protocolo para los clientes WebSocket (respuestas al que preguntó). */
Channel &wifiChannel();

/** @return true si hay al menos un cliente WebSocket conectado. */
bool wifiHasClients();

/** @return true si todos los clientes pueden recibir otra trama sin llenar su cola. */
bool wifiCanSend();

/** Escribe el estado: modo, red, IP, clientes, clave del AP. Nunca la clave de la red STA. */
void wifiStatusJson(JsonObject out);

/**
 * @brief Valida y guarda {mode, ssid, pass, ap_pass}; se aplica ~0.5 s después, para que la
 *        respuesta alcance a salir por la conexión actual.
 */
bool wifiConfigure(JsonObjectConst in, String &err);

/** Imprime en la consola cómo conectarse (red, clave, dirección). */
void wifiPrintInfo(Print &out);
