#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_Sensor.h>

Adafruit_LSM6DS3TRC imu;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting Foot IMU...");

  // XIAO ESP32-C3:
  // D4 = SDA
  // D5 = SCL
  Wire.begin(D4, D5);

  if (!imu.begin_I2C(0x6A, &Wire)) {
    Serial.println("ERROR: LSM6DS3TR-C not found!");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Foot LSM6DS3TR-C found!");
}

void loop() {
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

  delay(100);
}