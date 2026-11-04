#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_DRV2605.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLE2902.h>
#include <math.h>
#include "PacketDef.h"

// =============================================================================
// HARDWARE INSTANCES & CONFIGURATION
// =============================================================================
Adafruit_LSM6DS3TRC wristIMU;
Adafruit_DRV2605 drv;

// =============================================================================
// BLE COMMUNICATION HANDLES
// =============================================================================
// Server Role (Wrist Unit -> Mobile App)
BLEServer* pWristServer = nullptr;
BLECharacteristic* pWristDataChar = nullptr;
bool phoneConnected = false;

// Client Role (Wrist Unit <- Foot Pod)
BLEAdvertisedDevice* footPodDevice = nullptr;
BLERemoteCharacteristic* pFootRemoteChar = nullptr;
bool footConnected = false;
bool doScan = true;
bool doConnect = false;

// Incoming packet buffer from foot pod
ImuEventPacket latestFootPacket;
volatile bool newFootDataAvailable = false;

// =============================================================================
// HAPTIC FEEDBACK PATTERNS & THRESHOLDS
// =============================================================================
// Acceleration magnitude thresholds in Earth gravities (1g = 9.81 m/s^2)
const float THRESHOLD_LOW_G  = 1.8f; // Gentle anomaly / cadence deviation cue
const float THRESHOLD_MED_G  = 3.0f; // Moderate impact / excessive pronation
const float THRESHOLD_HIGH_G = 5.0f; // Severe impact / violent foot strike

// Asynchronous non-blocking haptic state machine
enum HapticState {
  HAPTIC_IDLE,
  HAPTIC_PLAYING,
  HAPTIC_COOLDOWN
};

HapticState hapticState = HAPTIC_IDLE;
unsigned long hapticStateStartTime = 0;
unsigned long currentPatternDurationMs = 0;

// Debounce lockout window to avoid buzzing continuously on sustained high impacts
const unsigned long HAPTIC_COOLDOWN_MS = 800;

/**
 * Triggers distinct DRV2605L ROM vibration waveforms based on impact intensity.
 * Uses millis() state tracking so it never blocks the main loop or drops BLE packets.
 */
void triggerPatternByAcceleration(float magnitudeG) {
  // If the motor is currently pulsing or in lockout cooldown, ignore new triggers
  if (hapticState != HAPTIC_IDLE) {
    return;
  }

  if (magnitudeG >= THRESHOLD_HIGH_G) {
    // TIER 3 (HIGH): Urgent double buzz for severe impact
    drv.setWaveform(0, 47);  // Buzz 1 - 100%
    drv.setWaveform(1, 100); // 100ms silent gap
    drv.setWaveform(2, 47);  // Buzz 1 - 100%
    drv.setWaveform(3, 0);   // End sequence delimiter
    drv.go();

    currentPatternDurationMs = 260; // Allow full double-pulse sequence to finish
    hapticState = HAPTIC_PLAYING;
    hapticStateStartTime = millis();
    Serial.printf("[HAPTIC ALERT] HIGH tier triggered! Magnitude: %.2fg\n", magnitudeG);

  } else if (magnitudeG >= THRESHOLD_MED_G) {
    // TIER 2 (MEDIUM): Distinct crisp click for sustained gait deviation
    drv.setWaveform(0, 1);   // Strong Click - 100%
    drv.setWaveform(1, 0);   // End sequence delimiter
    drv.go();

    currentPatternDurationMs = 120;
    hapticState = HAPTIC_PLAYING;
    hapticStateStartTime = millis();
    Serial.printf("[HAPTIC ALERT] MEDIUM tier triggered. Magnitude: %.2fg\n", magnitudeG);

  } else if (magnitudeG >= THRESHOLD_LOW_G) {
    // TIER 1 (LOW): Subtle bump for gentle informational correction
    drv.setWaveform(0, 15);  // Soft Bump - 60%
    drv.setWaveform(1, 0);   // End sequence delimiter
    drv.go();

    currentPatternDurationMs = 80;
    hapticState = HAPTIC_PLAYING;
    hapticStateStartTime = millis();
    Serial.printf("[HAPTIC ALERT] LOW tier triggered. Magnitude: %.2fg\n", magnitudeG);
  }
}

/**
 * Updates the haptic timer. Must be called on every loop() pass.
 */
void updateHapticStateMachine() {
  unsigned long now = millis();

  switch (hapticState) {
    case HAPTIC_IDLE:
      break;

    case HAPTIC_PLAYING:
      // Turn off driver output once waveform sequence duration expires
      if (now - hapticStateStartTime >= currentPatternDurationMs) {
        drv.stop();
        hapticState = HAPTIC_COOLDOWN;
        hapticStateStartTime = now;
      }
      break;

    case HAPTIC_COOLDOWN:
      // Enforce lockout window to prevent irritating rapid-fire buzzes
      if (now - hapticStateStartTime >= HAPTIC_COOLDOWN_MS) {
        hapticState = HAPTIC_IDLE; // Ready for next trigger
      }
      break;
  }
}

// =============================================================================
// BLE CLIENT CALLBACKS (RECEIVING STREAM FROM FOOT POD)
// =============================================================================
static void footNotifyCallback(
  BLERemoteCharacteristic* pChar,
  uint8_t* pData,
  size_t length,
  bool isNotify
) {
  // Validate incoming packet size matches our 16-byte binary struct contract
  if (length == sizeof(ImuEventPacket)) {
    memcpy(&latestFootPacket, pData, sizeof(ImuEventPacket));
    newFootDataAvailable = true;

    // Convert raw signed 16-bit values back to physical units (m/s^2)
    float ax = latestFootPacket.ax / 100.0f;
    float ay = latestFootPacket.ay / 100.0f;
    float az = latestFootPacket.az / 100.0f;

    // Calculate resultant 3D acceleration vector magnitude in g's
    float magnitudeG = sqrt((ax * ax) + (ay * ay) + (az * az)) / 9.81f;

    // Evaluate acceleration ranges and fire corresponding haptic waveform
    triggerPatternByAcceleration(magnitudeG);
  }
}

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
    footConnected = false;
    doScan = true; // Automatically rescan and reconnect if link is lost
    Serial.println("[BLE] Foot pod disconnected! Auto-rescanning...");
  }
};

class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Filter scan results by Foot Pod Service UUID
    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(BLEUUID(FOOT_SERVICE_UUID))) {
      BLEDevice::getScan()->stop();
      footPodDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

bool connectToFootPod() {
  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCallbacks());

  if (!pClient->connect(footPodDevice)) {
    return false;
  }

  BLERemoteService* pRemoteService = pClient->getService(FOOT_SERVICE_UUID);
  if (pRemoteService == nullptr) {
    pClient->disconnect();
    return false;
  }

  pFootRemoteChar = pRemoteService->getCharacteristic(FOOT_CHARACTERISTIC_UUID);
  if (pFootRemoteChar == nullptr) {
    pClient->disconnect();
    return false;
  }

  if (pFootRemoteChar->canNotify()) {
    pFootRemoteChar->registerForNotify(footNotifyCallback);
  }

  footConnected = true;
  return true;
}

// =============================================================================
// BLE SERVER CALLBACKS (ADVERTISING TELEMETRY TO MOBILE APP)
// =============================================================================
class WristServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    phoneConnected = true;
    Serial.println("[BLE] Mobile app connected to Wrist Unit.");
  }
  void onDisconnect(BLEServer* pServer) {
    phoneConnected = false;
    delay(200);
    pServer->startAdvertising(); // Resume advertising to mobile app
    Serial.println("[BLE] Mobile app disconnected. Resumed advertising.");
  }
};

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // 1. Initialize Wrist IMU
  if (!wristIMU.begin_I2C(0x6A)) {
    Serial.println("[ERROR] Wrist LSM6DS3 not detected at 0x6A!");
  } else {
    wristIMU.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
    wristIMU.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);
    Serial.println("[OK] Wrist LSM6DS3 initialized.");
  }

  // 2. Initialize DRV2605L Haptic Driver
  if (!drv.begin(&Wire)) {
    Serial.println("[ERROR] DRV2605L not found at 0x5A!");
  } else {
    drv.selectLibrary(1);             // Library 1: Standard ERM eccentric motor library
    drv.setMode(DRV2605_MODE_INTTRIG); // Internal trigger mode
    Serial.println("[OK] DRV2605L haptic driver initialized.");
  }

  // 3. Initialize Dual-Role BLE Subsystem
  BLEDevice::init("StrideGuard_Wrist");

  // Server link setup (Wrist -> Mobile App)
  pWristServer = BLEDevice::createServer();
  pWristServer->setCallbacks(new WristServerCallbacks());
  BLEService* pWristService = pWristServer->createService(WRIST_SERVICE_UUID);
  pWristDataChar = pWristService->createCharacteristic(
    WRIST_DATA_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pWristDataChar->addDescriptor(new BLE2902());
  pWristService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(WRIST_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  // Client link setup (Wrist <- Foot Pod)
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  Serial.println("[READY] StrideGuard Wrist Unit online. Scanning for foot pod...");
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
  // Constantly service background haptic timer so motor stops on time
  updateHapticStateMachine();

  // Handle auto-connection to detected foot pod
  if (doConnect) {
    if (connectToFootPod()) {
      Serial.println("[BLE] Successfully connected to Foot Pod!");
    } else {
      Serial.println("[BLE] Failed connection to Foot Pod.");
    }
    doConnect = false;
  }

  // Auto-rescan for foot pod if link dropped
  if (!footConnected && doScan) {
    BLEDevice::getScan()->start(3, false);
  }

  // Telemetry relay & monitoring loop (50 Hz interval)
  static unsigned long lastRelayTime = 0;
  if (millis() - lastRelayTime >= 20) {
    lastRelayTime = millis();

    // Sample local wrist sensor
    sensors_event_t wAccel, wGyro, wTemp;
    wristIMU.getEvent(&wAccel, &wGyro, &wTemp);

    // Relay foot pod packet onward to the connected mobile phone
    if (phoneConnected && newFootDataAvailable) {
      pWristDataChar->setValue((uint8_t*)&latestFootPacket, sizeof(latestFootPacket));
      pWristDataChar->notify();
    }

    // Diagnostic Serial output
    if (newFootDataAvailable) {
      float ax = latestFootPacket.ax / 100.0f;
      float ay = latestFootPacket.ay / 100.0f;
      float az = latestFootPacket.az / 100.0f;
      float magG = sqrt(ax * ax + ay * ay + az * az) / 9.81f;

      Serial.printf("Foot Mag: %4.2fg | Accel: [%5.2f, %5.2f, %5.2f] m/s^2 | Wrist T: %.1fC\n",
                    magG, ax, ay, az, wTemp.temperature);

      newFootDataAvailable = false;
    }
  }
}
