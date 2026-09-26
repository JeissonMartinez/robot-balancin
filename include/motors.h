/**
 * @file motors.h
 * @brief Control de dos motores DC con TB6612FNG mediante PWM LEDC (ESP32-S3).
 *
 * Convención de dirección:
 *  - PWM > 0 (adelante): xIN1 LOW,  xIN2 HIGH.
 *  - PWM < 0 (atrás):    xIN1 HIGH, xIN2 LOW.
 * El duty efectivo es |PWM| acotado a 0..255 (resolución de 8 bits).
 *
 * El puente H arranca deshabilitado (STBY LOW). Sólo TaskBalanceo habilita o
 * deshabilita los motores después de setup().
 */
#pragma once
#include <Arduino.h>

/** Configura pines de dirección, STBY (en LOW) y los dos canales PWM. */
void setupMotors();

/**
 * @brief Aplica PWMs con signo a los dos motores.
 * @param pwmL PWM motor izquierdo (CH_A), -255..255.
 * @param pwmR PWM motor derecho (CH_B), -255..255.
 */
void driveMotorsDifferential(float pwmL, float pwmR);

/**
 * @brief Compensa la zona muerta del motor (PWM_DEADBAND) de forma continua.
 *
 * |u| >= PWM_DEADBAND_BLEND: salida = signo(u)·(DB + |u|·(255 - DB)/255).
 * |u| <  PWM_DEADBAND_BLEND: interpolación lineal desde 0 hasta ese punto (sin salto en 0).
 * @param pwm Salida del controlador, -255..255.
 * @return PWM a aplicar al motor, -255..255.
 */
float compensateDeadband(float pwm);

/** Pone STBY en HIGH (habilita el puente H). */
void enableMotors();

/** Pone el duty a 0 en ambos canales y STBY en LOW. */
void stopMotors();
