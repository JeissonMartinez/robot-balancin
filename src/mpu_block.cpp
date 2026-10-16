#include "mpu_block.h"
#include "config.h"
#include <Wire.h>
#include <MPU6050.h>
#include <Preferences.h>
#include "console.h"

static MPU6050 mpu;
static Preferences prefs;

static float ax_offset = 0, ay_offset = 0, az_offset = 0;
static float gx_offset = 0;
static float gz_offset = 0;   // calibrado (NVS "gz_off"; 0 si la calibración es anterior a fw 0.9.0)
static float gz_bias = 0;     // deriva aprendida con el robot quieto (°/s), se suma a gz_offset
static bool learnYawBias = false;
static float angleFiltered = 0;
static float angularRate = 0;
static float yawRate = 0;

static float accelAngle(int16_t ay, int16_t az)
{
  float accelY = (ay - ay_offset) / 16384.0f;
  float accelZ = (az - az_offset) / 16384.0f;
  return atan2f(accelY, accelZ) * 180.0f / PI;
}

bool setupMPU(uint8_t dlpfMode)
{
  Wire.begin(I2C_SDA, I2C_SCL);
  mpu.initialize();
  if (!mpu.testConnection())
    return false;
  mpu.setDLPFMode(dlpfMode);
  return true;
}

void setMPUFilter(uint8_t dlpfMode)
{
  mpu.setDLPFMode(dlpfMode);
}

static void saveCalibration()
{
  prefs.begin("mpu", false);
  prefs.putFloat("ax_off", ax_offset);
  prefs.putFloat("ay_off", ay_offset);
  prefs.putFloat("az_off", az_offset);
  prefs.putFloat("gx_off", gx_offset);
  prefs.putFloat("gz_off", gz_offset);
  prefs.end();
  Console.println(">> Calibración guardada.");
}

bool loadCalibration()
{
  prefs.begin("mpu", true);
  if (!prefs.isKey("ax_off"))
  {
    prefs.end();
    return false;
  }
  ax_offset = prefs.getFloat("ax_off");
  ay_offset = prefs.getFloat("ay_off");
  az_offset = prefs.getFloat("az_off");
  gx_offset = prefs.getFloat("gx_off");
  gz_offset = prefs.getFloat("gz_off", 0.0f);
  prefs.end();
  return true;
}

void calibrateMPU()
{
  long ax_sum = 0, ay_sum = 0, az_sum = 0, gx_sum = 0, gz_sum = 0;
  const int samples = 500;

  for (int i = 0; i < samples; i++)
  {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    ax_sum += ax;
    ay_sum += ay;
    az_sum += az;
    gx_sum += gx;
    gz_sum += gz;
    delay(3);
  }

  ax_offset = (float)ax_sum / samples;
  ay_offset = (float)ay_sum / samples;
  az_offset = (float)az_sum / samples - 16384.0f;
  gx_offset = (float)gx_sum / samples;
  gz_offset = (float)gz_sum / samples;
  gz_bias = 0;

  saveCalibration();
}

void resetAngleFromAccel()
{
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  angleFiltered = accelAngle(ay, az);
}

float updateAngle(float dt)
{
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  float gyroX = (gx - gx_offset) / 131.0f;
  angularRate = gyroX;
  // α = τ/(τ + dt): misma constante de tiempo con cualquier periodo (0.98 a 50 Hz)
  const float alpha = ANGLE_FILTER_TAU_S / (ANGLE_FILTER_TAU_S + dt);
  angleFiltered = alpha * (angleFiltered + gyroX * dt) + (1.0f - alpha) * accelAngle(ay, az);

  // Giro alrededor del eje vertical (rumbo, M4b). Con el robot quieto y sin control, la lectura
  // es sólo deriva del sensor: se aprende con un pasa-bajas lento.
  const float gyroZ = (gz - gz_offset) / 131.0f;
  if (learnYawBias && fabsf(gyroX) < 2.0f && fabsf(gyroZ - gz_bias) < 3.0f)
    gz_bias += (gyroZ - gz_bias) * dt / (YAW_BIAS_TAU_S + dt);
  yawRate = gyroZ - gz_bias;
  return angleFiltered;
}

float getYawRate()
{
  return yawRate;
}

void mpuLearnYawBias(bool on)
{
  learnYawBias = on;
}

float getAngularRate()
{
  return angularRate;
}
