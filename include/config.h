/**
 * @file config.h
 * @brief Configuración de pines y parámetros del robot balancín (ESP32-S3).
 *
 * Secciones:
 *  - BOTÓN:     BTN_CAL, botón de calibración de la MPU (activo en LOW).
 *  - I2C / MPU: pines SDA/SCL del MPU6050.
 *  - MOTORES:   pines del puente H TB6612FNG (dirección, PWM y STBY).
 *  - PWM:       frecuencia, resolución y canales/timers LEDC.
 *  - ENCODERS:  pines de cuadratura y relación de pulsos por vuelta de rueda.
 *  - CONTROL:   periodo del lazo, límites de seguridad y telemetría.
 *  - Parámetros de ajuste (estructura, ganancias, RN, zona muerta, seguridad, DLPF):
 *    los valores de este archivo son los de FÁBRICA. En ejecución se usan los de
 *    params.cpp, que se ajustan por el protocolo (docs/hmi/PROTOCOLO.md) y, si se
 *    guardaron con "save", se cargan de NVS al arrancar y mandan sobre estos.
 *    Para volver a estos valores: comando "defaults" y luego "save".
 *
 * Notas de hardware:
 *  - GPIO 3 (R_A) y GPIO 46 (R_B) son pines de arranque (strapping) del ESP32-S3.
 *    Con GPIO 0 en alto (arranque normal) no afectan, pero si el robot no entra en
 *    modo descarga o no arranca, desconectar el encoder derecho al programar.
 */
#pragma once
#include <Arduino.h>
#include <driver/ledc.h>

// ===================== VERSIONES ===============================
#define FW_VERSION "0.5.0"
const int PROTOCOL_VERSION = 1;              // docs/hmi/PROTOCOLO.md
const uint32_t SERIAL_BAUD = 921600;         // 50 Hz de telemetría JSON ≈ 9 kB/s

// ===================== WIFI (wifi_link.h) ======================
// Por defecto el robot crea su red "Balancin-XXXX" (clave "bal-xxxxxx", única por chip).
// Modo, red y claves se cambian desde la HMI o con el comando "wifi" y quedan en NVS.
const uint32_t WIFI_STA_TIMEOUT_MS = 15000;  // sin conectar a la red local → levanta su AP
const uint8_t WIFI_MAX_WS_CLIENTS = 3;       // clientes WebSocket simultáneos

// ===================== BOTÓN ==================================
#define BTN_CAL 10
const uint32_t BTN_DEBOUNCE_MS = 50;

// ===================== I2C / MPU ==============================
#define I2C_SDA 41
#define I2C_SCL 42
// Filtro pasa-bajas digital interno del MPU6050 (registro CONFIG). Por defecto está en
// 256 Hz y el giroscopio capta la vibración de motores y el golpe del juego de la
// reductora, que la acción derivativa amplifica. 3 = 42 Hz (retardo ≈ 4.8 ms).
// Valores: 0=256, 1=188, 2=98, 3=42, 4=20, 5=10, 6=5 Hz.
const uint8_t MPU_DLPF_MODE = 3;

// ===================== PINES MOTORES ==========================
#define AIN1 7
#define AIN2 6
#define PWMA 5
#define BIN1 16
#define BIN2 17
#define PWMB 18
#define STBY 15

// ===================== PWM ====================================
#define PWM_FREQ 20000
#define PWM_RES 8
const ledc_channel_t CH_A = LEDC_CHANNEL_0; // Motor Izquierdo (A)
const ledc_channel_t CH_B = LEDC_CHANNEL_1; // Motor Derecho (B)
const ledc_timer_t TIMER_A = LEDC_TIMER_0;
const ledc_timer_t TIMER_B = LEDC_TIMER_1;

// ===================== PINES ENCODERS ==========================
#define R_A 3
#define R_B 46
#define L_A 9
#define L_B 11
// Motorreductor 25GA370-12V-100RPM con encoder, alimentado con LiPo 2S (8.4 V):
// velocidad máxima esperada ≈ 100 · 8.4/12 ≈ 70 RPM en la rueda.
const int CPR = 44;                          // Pulsos por vuelta del eje del motor
const float GEAR_RATIO = 119.0f;             // Reducción de la caja
const float PPR_WHEEL = CPR * GEAR_RATIO;    // Pulsos por vuelta de rueda
// Signo de cada encoder para que PWM > 0 (adelante) dé RPM > 0.
// La telemetría mostró RPM negativas con PWM positivo; verificar girando cada rueda a mano.
const int ENC_L_SIGN = -1;
const int ENC_R_SIGN = -1;
// Encoder izquierdo defectuoso (no cuenta al girar la rueda a mano y mete picos de ruido
// con los motores en marcha): la velocidad del robot se toma sólo de la rueda derecha.
const bool USE_LEFT_ENCODER = false;

// ===================== CONTROL ================================
const uint32_t CONTROL_PERIOD_MS = 20;       // Periodo de TaskBalanceo (50 Hz)
const float PWM_LIMIT = 255.0f;              // Saturación de PWM (8 bits)
const float MAX_ANGLE = 40.0f;               // |ángulo| > MAX_ANGLE => robot caído, motores off
const float REARM_ANGLE = 5.0f;              // |ángulo| < REARM_ANGLE => se reanuda el control
// Compensación de zona muerta de los motores. La prueba 'd' midió que la rueda arranca con
// PWM ≈ 22-26 (con el robot apoyado). Se compensa algo menos (arrancar desde parado cuesta
// más que mantener el giro; compensar de más produce oscilación). 0 = desactivada.
const float PWM_DEADBAND = 14.0f;
// Por debajo de |PWM| < PWM_DEADBAND_BLEND la compensación crece linealmente desde 0, para
// que el ruido del controlador en reposo no se convierta en golpes de ±PWM_DEADBAND.
const float PWM_DEADBAND_BLEND = 4.0f;
// La tarea de control publica una trama por ciclo (50 Hz). Por Serial se envía 1 de cada
// TELEMETRY_DIV_DEFAULT (5 = 10 Hz, legible en el monitor); el comando "tel" lo cambia.
const uint8_t TELEMETRY_DIV_DEFAULT = 5;

// ===================== PRUEBA DE ZONA MUERTA ('d' por Serial) ==
const int DEADBAND_TEST_MAX_PWM = 120;       // PWM máximo de la rampa
const uint32_t DEADBAND_TEST_STEP_MS = 80;   // Tiempo en cada escalón de PWM
// El encoder está en el eje del motor (antes de la reductora 1:119): al arrancar, el motor
// recorre primero el juego de los engranajes y genera pulsos sin que la rueda gire. Por eso
// se exige giro sostenido y en el sentido pedido durante varios escalones seguidos.
const long DEADBAND_TEST_PULSES = 8;         // Pulsos mínimos por escalón (en el sentido pedido)
const int DEADBAND_TEST_CONFIRM_STEPS = 3;   // Escalones seguidos con giro para darlo por válido
const uint32_t DEADBAND_TEST_SETTLE_MS = 1500; // Reposo antes de cada rampa

// ===================== ESTRUCTURA DE CONTROL =====================
// AngleOuter: estructura original. PID de ángulo (externo) -> referencia de velocidad
//             -> PI de velocidad (interno) -> PWM.
// SpeedOuter: estructura estándar de balancín. PI de velocidad (externo, lento) -> ángulo
//             deseado -> PD de ángulo (interno, 50 Hz) -> PWM. Corrige la deriva de posición.
// Se puede cambiar en ejecución (parámetro "structure"); los parámetros de ambas se conservan
// y el controlador se reinicia al cambiar.
enum class ControlStructure
{
  AngleOuter,
  SpeedOuter
};
const ControlStructure CONTROL_STRUCTURE = ControlStructure::SpeedOuter;

// --- Sólo SpeedOuter ---
// PWM = ANGLE_LOOP_PWM_GAIN · (Kp_angle·e + Kd_angle·de/dt). Igual a Kp_speed de la estructura
// original, para que KP_MIN/KP_MAX y Kd_angle den la misma ganancia efectiva en ambas.
const float ANGLE_LOOP_PWM_GAIN = 0.8f;
const uint32_t SPEED_LOOP_PERIOD_MS = 100;   // Periodo del lazo externo (10 Hz)
const float SPEED_REF_RPM = 0.0f;            // Velocidad deseada (0 = quedarse en su sitio)
const float MAX_TILT_REF_DEG = 4.0f;         // Límite del ángulo deseado que pide el lazo externo
// Signo del lazo externo: angle_ref = setpoint + SPEED_LOOP_SIGN · (Kp_v·v + Ki_v·∫v).
// Con estos motorreductores (1:119, casi fuentes de velocidad) y un lazo interno PD, un ángulo
// deseado > 0 se traduce en régimen permanente en avance (v ≈ 15 RPM por grado), así que el
// signo +1 hacía realimentación positiva: el robot se alejaba cada vez más rápido. -1 = negativa.
// Si con -1 el robot se aleja cada vez más rápido y Ref se queda en ±MAX_TILT_REF_DEG, volver a +1.
const float SPEED_LOOP_SIGN = -1.0f;

// ===================== GANANCIAS (valores de fábrica) ============
const float SETPOINT_ANGLE = 0.0f;           // Referencia del lazo de ángulo [°]
const float KD_ANGLE = 1.25f;                // ≈ Kd = 1 de la sintonía manual (PWM ≈ 0.8·salida)
const float KI_ANGLE = 1.0f;                 // Sólo AngleOuter
const float KP_SPEED = 0.8f;                 // Sólo AngleOuter (lazo interno de velocidad)
const float KI_SPEED = 0.0f;                 // Integral de velocidad desactivado hasta verificar encoders
// Lazo externo de velocidad (SpeedOuter). Ganancia de lazo ≈ 15 RPM/° · Kp_v; con 0.10 era
// 1.5 (> 1). Ki_v corrige que el cero calibrado no sea el punto de equilibrio real.
const float KP_V = 0.05f;                    // [°/RPM]
const float KI_V = 0.03f;                    // [°/(RPM·s)]

// ===================== RED NEURONAL (Kp adaptativa) ============
// Kp_angle = KP_MIN + (KP_MAX - KP_MIN) * salida_RN, con salida_RN en [0, 1].
// Ganancia efectiva ángulo→PWM ≈ Kp_speed · Kp_angle = 0.8 · Kp_angle. El rango 55..85
// equivale a 44..68 PWM/°, dentro de lo que la sintonía manual del informe halló estable
// (Kp 50-70; con 80 ya oscilaba).
const float KP_MIN = 70.0f; //55.0 // 70.0
const float KP_MAX = 70.0f; // 85.0  //70.0
// La RN sube Kp si |error de ángulo| > NN_ERROR_BAND y la baja si es menor [°].
// Más ancha que el ruido del ángulo para que Kp no suba con cada pequeña oscilación.
const float NN_ERROR_BAND = 1.0f;
// Tasa de aprendizaje del entrenamiento en línea. Con 2.25 los pesos crecían hasta que la
// RN conmutaba Kp entre KP_MIN y KP_MAX en un solo ciclo (efecto relé -> vibración).
const float NN_LEARNING_RATE = 0.5f;
// Constante de tiempo del filtro pasa-bajas aplicado a la Kp que propone la RN [s].
// Kp no puede cambiar más rápido que esto, aunque la salida de la RN salte.
const float KP_FILTER_TAU = 0.3f;
// true: la acción derivativa usa la velocidad angular del giroscopio (-ω) en vez de
// diferenciar el error; menos ruido y sin el retardo de un periodo.
const bool USE_GYRO_DERIVATIVE = true;
