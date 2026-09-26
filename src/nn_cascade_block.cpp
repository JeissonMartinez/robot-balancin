#include "nn_cascade_block.h"
#include "config.h"
#include "Neural_Networks_FF.h"

// ===================== RN ===========================================
static Neural_Networks_FF net = Neural_Networks_FF();
static Dynamic_Array XI = Dynamic_Array();
static Dynamic_Array D = Dynamic_Array();
static Dynamic_Array FAC_STR = Dynamic_Array();
static Dynamic_Array EST = Dynamic_Array();
static Dynamic_Array posicion = Dynamic_Array();

// ===================== PARÁMETROS ===================================
float setpoint_angle = 0.0;

float Kp_angle = KP_MIN;
float Ki_angle = 1.0;
float Kd_angle = 1.25; // ≈ Kd = 1 de la sintonía manual (PWM ≈ 0.8·salida del lazo de ángulo)

float Kp_speed = 0.8;
float Ki_speed = 0.0; // Integral de velocidad desactivado hasta verificar encoders

// Lazo externo de velocidad (ControlStructure::SpeedOuter). Ganancia de lazo ≈ 15 RPM/° · Kp_v;
// con 0.10 era 1.5 (> 1). Ki_v corrige que el cero calibrado no sea el punto de equilibrio real.
float Kp_v = 0.05;  // [°/RPM]
float Ki_v = 0.03;  // [°/(RPM·s)]

// ===================== ESTADO =======================================
static float error = 0.00;
static float error_1 = 0.00;
static float error_2 = 0.00;
static float Output = 0.00;

static float angle_integral = 0;
static float angle_prev_error = 0;
static float speed_integral = 0;

// Lazo externo de velocidad (sólo ControlStructure::SpeedOuter)
static float angle_ref = 0;          // Ángulo deseado que fija el lazo de velocidad [°]
static float outer_speed_integral = 0;
static float outer_rpm_sum = 0;
static float outer_time = 0;
static int outer_samples = 0;

void initNeural()
{
  XI.NewArray_2D(1, 3);
  XI.ToinitializeArray_2D(0.0);

  D.NewArray_2D(1, 1);
  D.ToinitializeArray_2D(0.0);

  byte XC = XI.GetColumns();
  byte DF = D.GetRows();
  byte L = 3;

  EST.NewArray_2D(1, L);
  EST.ToinitializeArray_2D(0.0);
  float NC = EST.GetColumns();

  float V[] = {(float)XC, 3, (float)DF};
  for (byte i = 0; i < NC; i++)
  {
    EST.WriteArray_2D(0, i, V[i]);
  }

  FAC_STR.CharNewArray_1D(L);
  char *myStrings[] = {(char *)"logsig", (char *)"logsig", (char *)"poslin_lim"};
  for (byte i = 0; i < FAC_STR.GetRows(); i++)
  {
    FAC_STR.CharWriteArray_1D(i, myStrings[i]);
  }

  net.dposlin_limits(1, 0); // Salida en [0, 1] -> Kp siempre positiva
  net.LearningRate = 0.00;
  net.FEED_FORWARD_NET(EST, FAC_STR);
  posicion.NewArray_2D(1, 1);
  posicion.ToinitializeArray_2D(0.0);
  net.LearningRate = NN_LEARNING_RATE;
}

void resetCascade(float angle)
{
  angle_ref = setpoint_angle;
  error = angle_ref - angle;
  error_1 = error;
  error_2 = error;
  angle_prev_error = error;
  angle_integral = 0;
  speed_integral = 0;
  outer_speed_integral = 0;
  outer_rpm_sum = 0;
  outer_time = 0;
  outer_samples = 0;
}

float getAngleReference()
{
  return (CONTROL_STRUCTURE == ControlStructure::SpeedOuter) ? angle_ref : setpoint_angle;
}

// RN: ajuste en línea de Kp_angle a partir del error de ángulo actual (común a ambas
// estructuras). Error de entrenamiento = (|e| - NN_ERROR_BAND)/100: > 0 sube Kp, < 0 la baja.
static void updateNeuralKp(float dt)
{
  posicion.WriteArray_2D(0, 0, NN_ERROR_BAND / 100.00);
  XI.WriteArray_2D(0, 0, error);
  XI.WriteArray_2D(0, 1, error - error_1);
  XI.WriteArray_2D(0, 2, error_1 - error_2);
  net.NORMALIZE_DATA(XI, 100.00);
  D.WriteArray_2D(0, 0, fabsf(error) / 100.00);
  net.TRAIN_NET_ONLINE(XI, D, posicion);
  Output = net.y.ReadArray_2D(net.y.GetRows() - 1, 0);
  error_2 = error_1;
  error_1 = error;
  float kp_target = KP_MIN + (KP_MAX - KP_MIN) * Output;
  Kp_angle += (dt / (KP_FILTER_TAU + dt)) * (kp_target - Kp_angle);
}

static float angleDerivative(float angleRate, float dt)
{
  return USE_GYRO_DERIVATIVE ? -angleRate : (error - angle_prev_error) / dt;
}

// ---------------------------------------------------------------------------------
// Estructura original: ángulo (PID, externo) -> referencia de velocidad -> PI -> PWM
// ---------------------------------------------------------------------------------
static float cascadeAngleOuter(float angleRate, float speed_measured, float dt)
{
  // --- Lazo de ángulo (PID) -> referencia de velocidad ---
  float derivative = angleDerivative(angleRate, dt);
  float pid_unsat = Kp_angle * error + Ki_angle * angle_integral + Kd_angle * derivative;

  float speed_ref = constrain(pid_unsat, -300, 300);
  if (!((pid_unsat != speed_ref) && (error * pid_unsat) > 0))
  {
    angle_integral += error * dt;
  }
  angle_prev_error = error;

  // --- Lazo de velocidad (PI) -> PWM ---
  float error_speed = speed_ref - speed_measured;

  float pwm_unsat = Kp_speed * error_speed + Ki_speed * speed_integral;
  float pwm_balanceo_base = constrain(pwm_unsat, -PWM_LIMIT, PWM_LIMIT);

  if (!((pwm_unsat != pwm_balanceo_base) && (error_speed * pwm_unsat) > 0))
  {
    speed_integral += error_speed * dt;
  }

  speed_integral = constrain(speed_integral, -150, 150);

  return pwm_balanceo_base;
}

// ---------------------------------------------------------------------------------
// Estructura estándar de balancín: velocidad (PI, externo, lento) -> ángulo deseado
//                                  ángulo (PD, interno, 50 Hz) -> PWM
// ---------------------------------------------------------------------------------
// Convenio de signos (verificado con la telemetría): PWM > 0 mueve las ruedas hacia
// adelante (RPM > 0) y corrige ángulos negativos, así que ángulo < 0 = inclinado hacia
// adelante. Con el lazo interno PD, en régimen permanente el robot avanza a v ≈ c·(angle_ref
// - ángulo de equilibrio), así que para frenar un avance hay que bajar angle_ref:
// angle_ref = setpoint + SPEED_LOOP_SIGN·(Kp_v·v + Ki_v·∫v), con SPEED_LOOP_SIGN = -1.
// El término integral (∫v ∝ distancia recorrida) devuelve el robot a su sitio y absorbe la
// diferencia entre el cero calibrado y el punto de equilibrio real.
static void updateSpeedLoop(float speed_measured, float dt)
{
  outer_rpm_sum += speed_measured;
  outer_time += dt;
  outer_samples++;
  if (outer_time * 1000.0f < SPEED_LOOP_PERIOD_MS)
    return;

  float v = outer_rpm_sum / outer_samples; // Promedio en el periodo del lazo externo
  float T = outer_time;
  outer_rpm_sum = 0;
  outer_time = 0;
  outer_samples = 0;

  float v_error = v - SPEED_REF_RPM;
  float tilt_unsat = SPEED_LOOP_SIGN * (Kp_v * v_error + Ki_v * outer_speed_integral);
  float tilt = constrain(tilt_unsat, -MAX_TILT_REF_DEG, MAX_TILT_REF_DEG);
  // Anti-windup: no integrar si está saturado y el error empuja hacia la saturación
  if (!((tilt_unsat != tilt) && (SPEED_LOOP_SIGN * v_error * tilt_unsat) > 0))
  {
    outer_speed_integral += v_error * T;
  }
  angle_ref = setpoint_angle + tilt;
}

static float cascadeSpeedOuter(float angleRate, float speed_measured, float dt)
{
  // Lazo interno PD de ángulo -> PWM. ANGLE_LOOP_PWM_GAIN = Kp_speed de la estructura
  // original, para que KP_MIN/KP_MAX y Kd_angle signifiquen lo mismo en ambas estructuras.
  float derivative = angleDerivative(angleRate, dt);
  angle_prev_error = error;
  float pwm_unsat = ANGLE_LOOP_PWM_GAIN * (Kp_angle * error + Kd_angle * derivative);
  return constrain(pwm_unsat, -PWM_LIMIT, PWM_LIMIT);
}

float cascada(float angle, float angleRate, float rpmLeft, float rpmRight, float dt)
{
  float speed_measured = USE_LEFT_ENCODER ? (rpmLeft + rpmRight) * 0.5f : rpmRight;

  if (CONTROL_STRUCTURE == ControlStructure::SpeedOuter)
  {
    updateSpeedLoop(speed_measured, dt);
    error = angle_ref - angle;
    updateNeuralKp(dt);
    return cascadeSpeedOuter(angleRate, speed_measured, dt);
  }

  error = setpoint_angle - angle;
  updateNeuralKp(dt);
  return cascadeAngleOuter(angleRate, speed_measured, dt);
}
