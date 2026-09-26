#include <Arduino.h>

#include "config.h"
#include "encoders.h"
#include "motors.h"
#include "mpu_block.h"
#include "nn_cascade_block.h"
#include "params.h"
#include "protocol.h"
#include "tasks_block.h"

// Devuelve true una vez por pulsación: LOW estable BTN_DEBOUNCE_MS y luego soltado.
static bool calibrationButtonPressed()
{
  if (digitalRead(BTN_CAL) != LOW)
    return false;
  delay(BTN_DEBOUNCE_MS);
  if (digitalRead(BTN_CAL) != LOW)
    return false;
  while (digitalRead(BTN_CAL) == LOW)
    delay(10);
  return true;
}

static void printHelp()
{
  Serial.println();
  Serial.println("Comandos (escribir la letra en el monitor serie):");
  Serial.println("  d  prueba de zona muerta de motores");
  Serial.println("  c  calibrar MPU (igual que el botón)");
  Serial.println("  t  pausar / reanudar la telemetría");
  Serial.println("  j  telemetría en JSON a 50 Hz / en texto a 10 Hz");
  Serial.println("  e  parada de emergencia (motores off, no se re-arma)");
  Serial.println("  a  liberar la parada de emergencia");
  Serial.println("  p  mostrar los parámetros activos (JSON)");
  Serial.println("  ?  esta ayuda");
  Serial.println("Las líneas que empiezan con '{' son comandos JSON (docs/hmi/PROTOCOLO.md).");
}

static void reportPaused(bool ok)
{
  Serial.println(ok ? ">> Control reanudado (se activa al poner el robot vertical)."
                    : ">> ERROR: la tarea de control no se detuvo. Acción cancelada.");
}

static void handleKey(char cmd)
{
  switch (cmd)
  {
  case 'd':
  case 'D':
    reportPaused(runDeadband());
    break;
  case 'c':
  case 'C':
    reportPaused(runCalibration());
    break;
  case 't':
  case 'T':
    telemetryOutput().enabled = !telemetryOutput().enabled;
    Serial.println(telemetryOutput().enabled ? ">> Telemetría reanudada." : ">> Telemetría en pausa.");
    break;
  case 'j':
  case 'J':
  {
    TelemetryOutput &o = telemetryOutput();
    o.json = !o.json;
    o.div = o.json ? 1 : TELEMETRY_DIV_DEFAULT;
    Serial.println(o.json ? ">> Telemetría JSON a 50 Hz." : ">> Telemetría en texto a 10 Hz.");
    break;
  }
  case 'e':
  case 'E':
    setEStop(true);
    Serial.println(">> PARADA DE EMERGENCIA. Enviar 'a' para liberar.");
    break;
  case 'a':
  case 'A':
    setEStop(false);
    Serial.println(">> Parada liberada. El control se activa al poner el robot vertical.");
    break;
  case 'p':
  case 'P':
    protocolWriteParams(Serial);
    break;
  case '?':
  case 'h':
  case 'H':
    printHelp();
    break;
  default:
    break; // Ignora saltos de línea y otras teclas
  }
}

// Separa la entrada del Serial: una línea que empieza con '{' es un comando JSON (hasta
// '\n'); cualquier otro carácter suelto es una tecla.
static void pollSerial()
{
  static char line[1024];
  static size_t len = 0;
  static bool inJson = false;
  static bool overflow = false;

  while (Serial.available())
  {
    char c = Serial.read();
    if (!inJson)
    {
      if (c == '{')
      {
        inJson = true;
        overflow = false;
        line[0] = c;
        len = 1;
      }
      else
        handleKey(c);
      continue;
    }

    if (c == '\n' || c == '\r')
    {
      inJson = false;
      if (overflow)
      {
        Serial.println("{\"type\":\"ack\",\"id\":null,\"ok\":false,\"err\":\"línea demasiado larga\"}");
        continue;
      }
      line[len] = '\0';
      protocolHandleLine(line, Serial);
    }
    else if (len < sizeof(line) - 1)
      line[len++] = c;
    else
      overflow = true;
  }
}

static void pumpTelemetry()
{
  Telemetry t;
  const TelemetryOutput &o = telemetryOutput();
  while (receiveTelemetry(t))
  {
    if (o.enabled && t.seq % o.div == 0)
      protocolWriteTelemetry(t, Serial, o.json);
  }
}

void setup()
{
  Serial.setRxBufferSize(1024);
  Serial.begin(SERIAL_BAUD);

  paramsInit();
  Params p;
  paramsSnapshot(p);

  setupMotors();
  pinMode(BTN_CAL, INPUT_PULLUP);

  if (!setupMPU(p.mpuDlpfMode))
  {
    Serial.println(">> ERROR: MPU6050 no responde. Revisar cableado I2C (SDA 41, SCL 42).");
    while (true)
      delay(1000);
  }

  if (loadCalibration())
    Serial.println(">> Calibración cargada de memoria.");
  else
    Serial.println(">> No existe calibración guardada. Necesitas presionar el botón.");

  setupEncoders();
  initNeural(p);
  resetAngleFromAccel();

  startControlTask();

  Serial.printf("Firmware %s · protocolo v%d\n", FW_VERSION, PROTOCOL_VERSION);
  Serial.printf("Parámetros: %s\n", paramsLoadedFromNvs() ? "guardados en NVS" : "de fábrica (config.h)");
  Serial.printf("Estructura de control: %s\n",
                p.structure == ControlStructure::SpeedOuter
                    ? "SpeedOuter (velocidad -> angulo -> PWM)"
                    : "AngleOuter (angulo -> velocidad -> PWM)");
  Serial.println("Robot listo. Ponlo vertical para activar el control; botón = calibrar MPU.");
  printHelp();
  protocolHello(Serial);
}

void loop()
{
  if (calibrationButtonPressed())
  {
    Serial.println(">> Botón presionado. Deteniendo robot para calibrar...");
    reportPaused(runCalibration());
  }

  pollSerial();
  pumpTelemetry();
  delay(5);
}
