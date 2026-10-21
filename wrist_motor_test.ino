#include <Wire.h>
#include <Adafruit_DRV2605.h>

Adafruit_DRV2605 drv;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting DRV2605L test...");

  if (!drv.begin()) {
    Serial.println("ERROR: DRV2605L not found!");
    while (1) {
      delay(10);
    }
  }

  Serial.println("DRV2605L found!");

  // Our coin motor is an ERM motor
  drv.useERM();

  // Use the built-in vibration effect library
  drv.selectLibrary(1);

  // Internal trigger mode
  drv.setMode(DRV2605_MODE_INTTRIG);
}

void loop() {
  Serial.println("Vibrate!");

  // Effect 1: strong click
  drv.setWaveform(0, 1);

  // End of waveform sequence
  drv.setWaveform(1, 0);

  // Trigger the effect
  drv.go();

  delay(2000);
}