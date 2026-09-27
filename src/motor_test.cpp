#include "motor_test.h"
#include "config.h"
#include "encoders.h"
#include "motors.h"
#include "console.h"

// Cualquier tecla por Serial o cualquier comando por WebSocket aborta la prueba.
static bool abortRequested()
{
  return consoleAbortRequested();
}

// Sube el PWM en un sentido hasta detectar giro sostenido de la rueda derecha.
// Devuelve el PWM del primer escalón de la racha de giro, 0 si no arrancó antes de
// DEADBAND_TEST_MAX_PWM, o -1 si se abortó.
static int rampUntilMotion(int direction)
{
  long pulsesL, pulsesR;
  readAndResetEncoders(pulsesL, pulsesR);

  int streak = 0;
  for (int pwm = 0; pwm <= DEADBAND_TEST_MAX_PWM; pwm++)
  {
    driveMotorsDifferential(direction * pwm, direction * pwm);
    delay(DEADBAND_TEST_STEP_MS);

    readAndResetEncoders(pulsesL, pulsesR);
    // Pulsos en el sentido pedido (ENC_R_SIGN hace que PWM > 0 dé pulsos > 0)
    long directed = direction * ENC_R_SIGN * pulsesR;
    streak = (directed >= DEADBAND_TEST_PULSES) ? streak + 1 : 0;
    if (streak >= DEADBAND_TEST_CONFIRM_STEPS)
      return pwm - (DEADBAND_TEST_CONFIRM_STEPS - 1);

    if (abortRequested())
      return -1;

    if (pwm % 10 == 0)
      Console.printf("   PWM %3d ...\n", pwm);
  }
  return 0;
}

static void printResult(const char *label, int pwm)
{
  if (pwm > 0)
    Console.printf(">> %s: la rueda derecha arranca con PWM = %d\n", label, pwm);
  else
    Console.printf(">> %s: no arrancó hasta PWM = %d\n", label, DEADBAND_TEST_MAX_PWM);
}

void runDeadbandTest()
{
  Console.println();
  Console.println("===== PRUEBA DE ZONA MUERTA DE MOTORES =====");
  Console.println("Sujeta el robot VERTICAL con las ruedas apoyadas en el suelo.");
  Console.println("Déjalo rodar con la mano suelta en horizontal; no lo empujes ni lo frenes.");
  Console.println("Los motores subirán el PWM poco a poco. Pulsa cualquier tecla para abortar.");

  for (int s = 3; s > 0; s--)
  {
    Console.printf("Inicia en %d...\n", s);
    delay(1000);
    if (abortRequested())
    {
      Console.println(">> Prueba abortada.");
      return;
    }
  }

  enableMotors();
  delay(DEADBAND_TEST_SETTLE_MS);

  Console.println("-> Adelante (PWM > 0)");
  int forward = rampUntilMotion(+1);
  stopMotors();
  if (forward < 0)
  {
    Console.println(">> Prueba abortada.");
    return;
  }
  enableMotors();
  delay(DEADBAND_TEST_SETTLE_MS);
  Console.println("-> Atrás (PWM < 0)");
  int backward = rampUntilMotion(-1);
  stopMotors();
  if (backward < 0)
  {
    Console.println(">> Prueba abortada.");
    return;
  }

  Console.println();
  printResult("Adelante", forward);
  printResult("Atrás   ", backward);
  if (forward > 0 && backward > 0)
    Console.printf(">> Zona muerta estimada: PWM ≈ %d (el mayor de los dos sentidos)\n",
                  max(forward, backward));
  Console.println("Observa si la rueda IZQUIERDA arrancó a la vez (su encoder no funciona).");
  Console.println("============================================");
}
