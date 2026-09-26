/**
 * @file nn_cascade_block.h
 * @brief Control en cascada con Kp de ángulo adaptada en línea por una red neuronal
 *        feed-forward (Neural_Networks_FF). Dos estructuras seleccionables con el
 *        parámetro structure (params.h). Los nombres de ganancias de estas fórmulas
 *        corresponden a campos de Params (Kd_angle = kdAngle, Kp_v = kpV, ...):
 *
 * SpeedOuter (estándar de balancín):
 *   angle_ref = setpoint_angle + sat±MAX_TILT( SPEED_LOOP_SIGN·(Kp_v·v + Ki_v·∫v) )  (10 Hz)
 *   pwm = sat±PWM_LIMIT( ANGLE_LOOP_PWM_GAIN·(Kp_angle·e + Kd_angle·de/dt) ),  e = angle_ref - angle
 *   v = RPM medida. Con SPEED_LOOP_SIGN = -1, v > 0 (avance) baja angle_ref, que es lo que
 *   frena al robot con este lazo interno PD y motores de reductora alta.
 *
 * Red neuronal:
 *  - Topología 3-3-1, activaciones logsig, logsig, poslin_lim (límites 0..1).
 *  - Entradas (normalizadas /100): [e(k), e(k)-e(k-1), e(k-1)-e(k-2)].
 *  - Kp objetivo = KP_MIN + (KP_MAX - KP_MIN) * salida: siempre positiva y acotada.
 *    Kp_angle sigue a ese objetivo con un filtro pasa-bajas (KP_FILTER_TAU) para que no
 *    cambie de golpe.
 *  - Se entrena en cada ciclo (TRAIN_NET_ONLINE) con error (|e| - NN_ERROR_BAND)/100:
 *    si el robot se aleja de la vertical más que la banda, Kp sube; si está dentro, baja.
 *
 * AngleOuter (original) - lazo de ángulo (PID, externo):
 *   speed_ref = sat±300( Kp_angle·e + Ki_angle·∫e + Kd_angle·de/dt ),  e = setpoint_angle - angle
 *   de/dt = -ω (giroscopio) si USE_GYRO_DERIVATIVE, o (e(k) - e(k-1))/dt si no.
 * AngleOuter - lazo de velocidad (PI, interno):
 *   pwm = sat±PWM_LIMIT( Kp_speed·(speed_ref - rpm) + Ki_speed·∫e_v )
 * En ambas estructuras: rpm = media de ambas ruedas, o sólo la derecha si
 * USE_LEFT_ENCODER es false; de/dt = -ω (giroscopio) si USE_GYRO_DERIVATIVE.
 * Todos los integradores tienen anti-windup condicional; ∫e_v además se limita a ±150.
 *
 * Uso: initNeural(p) una vez; resetCascade(p, angle) antes de (re)activar el control;
 *      cascada(...) cada periodo con dt > 0.
 */
#pragma once
#include <Arduino.h>
#include "params.h"

/**
 * @brief Aporte de cada término del lazo de ángulo al PWM, antes de saturar.
 *  - SpeedOuter: p = 0.8·Kp·e, i = 0, d = 0.8·Kd·de/dt  (p + d = PWM sin saturar).
 *  - AngleOuter: los términos del PID multiplicados por kp_speed, es decir, su aporte
 *    a través del PI de velocidad mientras speed_ref no sature. El PWM además incluye
 *    −kp_speed·v y la integral de velocidad, que no están aquí.
 */
struct ControlTerms
{
  float p, i, d;
};

/** Crea la topología de la red y sus arreglos. Llamar una vez en setup(), tras paramsInit(). */
void initNeural(const Params &p);

/**
 * @brief Reinicia integradores e historial de errores del controlador (no los pesos
 *        de la red), usando el ángulo actual para evitar un salto en la derivada.
 */
void resetCascade(const Params &p, float angle);

/**
 * @brief Ejecuta un paso del control en cascada (incluye entrenamiento de la RN).
 * @param p Parámetros de ajuste vigentes en este ciclo.
 * @param angle Ángulo medido [°].
 * @param angleRate Velocidad angular medida [°/s] (giroscopio).
 * @param rpmLeft, rpmRight RPM filtradas de cada rueda.
 * @param dt Tiempo desde el paso anterior [s].
 * @return PWM de balanceo saturado a ±PWM_LIMIT.
 */
float cascada(const Params &p, float angle, float angleRate, float rpmLeft, float rpmRight, float dt);

/** @return Ángulo deseado actual [°] (en SpeedOuter lo fija el lazo de velocidad). */
float getAngleReference();

/** @return Kp actual del lazo de ángulo (la ajusta la RN). */
float getKpAngle();

/** @return Términos P, I, D del último ciclo (cero con el control inactivo). */
ControlTerms getControlTerms();
