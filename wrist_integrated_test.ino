#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_DRV2605.h>

Adafruit_LSM6DS3TRC imu;
Adafruit_DRV2605 drv;

unsigned long lastVibration = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting Wrist Unit...");

  // ---------- IMU ----------
  if (!imu.begin_I2C(0x6A)) {
    Serial.println("ERROR: IMU not found!");
    while (1) {
      delay(10);
    }
  }

  Serial.println("IMU found!");

  // ---------- Haptic Driver ----------
  if (!drv.begin()) {
    Serial.println("ERROR: DRV2605L not found!");
    while (1) {
      delay(10);
    }
  }

  Serial.println("DRV2605L found!");

  drv.useERM();
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);

  Serial.println("Wrist Unit ready!");
}

void loop() {

  // ---------- Read IMU ----------
  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t temp;

  imu.getEvent(&accel, &gyro, &temp);

  Serial.print("Accel X: ");
  Serial.print(accel.acceleration.x);
  Serial.print("  Y: ");
  Serial.print(accel.acceleration.y);
  Serial.print("  Z: ");
  Serial.print(accel.acceleration.z);

  Serial.print("    Gyro X: ");
  Serial.print(gyro.gyro.x);
  Serial.print("  Y: ");
  Serial.print(gyro.gyro.y);
  Serial.print("  Z: ");
  Serial.println(gyro.gyro.z);

  // ---------- Vibrate every 3 seconds ----------
  if (millis() - lastVibration >= 3000) {

    Serial.println("VIBRATE!");

    drv.setWaveform(0, 1);
    drv.setWaveform(1, 0);
    drv.go();

    lastVibration = millis();
  }

  delay(100);
}