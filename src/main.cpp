#include <Arduino.h>

#include "config.h"
#include "encoders.h"
#include "motors.h"
#include "mpu_block.h"
#include "nn_cascade_block.h"
#include "tasks_block.h"
#include "motor_test.h"

static bool telemetryEnabled = true;

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

static void printTelemetry()
{
  Telemetry t;
  getTelemetry(t);
  Serial.printf("Ang: %.2f | Ref: %.2f | w: %.1f | PWM: %.1f | RPM L: %.1f | RPM R: %.1f | Kp: %.2f | dt: %.4f | %s\n",
                t.angle, t.angleRef, t.rate, t.pwm, t.rpmL, t.rpmR, t.kp, t.dt, t.active ? "ACTIVO" : "MOTORES OFF");
}

static void printHelp()
{
  Serial.println();
  Serial.println("Comandos (escribir la letra en el monitor serie):");
  Serial.println("  d  prueba de zona muerta de motores");
  Serial.println("  c  calibrar MPU (igual que el botón)");
  Serial.println("  t  pausar / reanudar la telemetría");
  Serial.println("  ?  esta ayuda");
}

// Detiene el control, ejecuta la acción y lo reanuda. Devuelve false si no se pudo pausar.
static bool runWithControlPaused(void (*action)())
{
  if (!pauseControl())
  {
    Serial.println(">> ERROR: la tarea de control no se detuvo. Acción cancelada.");
    resumeControl();
    return false;
  }
  action();
  resumeControl();
  Serial.println(">> Control reanudado (se activa al poner el robot vertical).");
  return true;
}

static void calibrate()
{
  Serial.println(">> Iniciando calibración MPU (robot quieto y vertical)...");
  calibrateMPU();
  Serial.println(">> Calibración MPU completa.");
}

static void handleSerialCommand()
{
  if (!Serial.available())
    return;
  char cmd = Serial.read();

  switch (cmd)
  {
  case 'd':
  case 'D':
    runWithControlPaused(runDeadbandTest);
    break;
  case 'c':
  case 'C':
    runWithControlPaused(calibrate);
    break;
  case 't':
  case 'T':
    telemetryEnabled = !telemetryEnabled;
    Serial.println(telemetryEnabled ? ">> Telemetría reanudada." : ">> Telemetría en pausa.");
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

void setup()
{
  Serial.begin(115200);

  setupMotors();
  pinMode(BTN_CAL, INPUT_PULLUP);

  if (!setupMPU())
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
  initNeural();
  resetAngleFromAccel();

  startControlTask();

  Serial.printf("Estructura de control: %s\n",
                CONTROL_STRUCTURE == ControlStructure::SpeedOuter
                    ? "SpeedOuter (velocidad -> angulo -> PWM)"
                    : "AngleOuter (angulo -> velocidad -> PWM)");
  Serial.println("Robot listo. Ponlo vertical para activar el control; botón = calibrar MPU.");
  printHelp();
}

void loop()
{
  if (calibrationButtonPressed())
  {
    Serial.println(">> Botón presionado. Deteniendo robot para calibrar...");
    runWithControlPaused(calibrate);
  }

  handleSerialCommand();

  static uint32_t lastPrint = 0;
  if (telemetryEnabled && millis() - lastPrint >= TELEMETRY_PERIOD_MS)
  {
    lastPrint = millis();
    printTelemetry();
  }
  delay(5);
}
