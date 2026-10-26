#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_DRV2605.h>

#include <BLEDevice.h>


// ======================================================
// BLE UUID
// ======================================================

static BLEUUID serviceUUID(
  "12345678-1234-1234-1234-1234567890ab"
);

static BLEUUID charUUID(
  "abcdefab-1234-1234-1234-abcdefabcdef"
);


// ======================================================
// Wrist hardware
// ======================================================

Adafruit_LSM6DS3TRC wristIMU;
Adafruit_DRV2605 drv;


// ======================================================
// Foot BLE packet
// Must be EXACTLY the same as Foot
// ======================================================

struct FootPacket {

  uint32_t timestamp;

  int16_t ax;
  int16_t ay;
  int16_t az;

  int16_t gx;
  int16_t gy;
  int16_t gz;
};


FootPacket latestFootPacket;

volatile bool newFootPacket = false;


// ======================================================
// BLE variables
// ======================================================

static bool doConnect = false;
static bool connected = false;

static BLERemoteCharacteristic *remoteCharacteristic;
static BLEAdvertisedDevice *footDevice;


// ======================================================
// Receive Foot BLE data
// ======================================================

static void notifyCallback(
  BLERemoteCharacteristic *characteristic,
  uint8_t *data,
  size_t length,
  bool isNotify
) {

  if (length != sizeof(FootPacket)) {

    Serial.print("Unexpected BLE packet size: ");
    Serial.println(length);

    return;
  }

  memcpy(
    &latestFootPacket,
    data,
    sizeof(FootPacket)
  );

  newFootPacket = true;
}


// ======================================================
// BLE connection callbacks
// ======================================================

class WristClientCallbacks : public BLEClientCallbacks {

  void onConnect(BLEClient *client) {

    Serial.println("Connected to Foot!");
  }


  void onDisconnect(BLEClient *client) {

    connected = false;

    Serial.println("Foot disconnected.");
  }
};


// ======================================================
// Connect to Foot
// ======================================================

bool connectToFoot() {

  Serial.println("Connecting to StrideGuard Foot...");

  BLEClient *client =
    BLEDevice::createClient();

  client->setClientCallbacks(
    new WristClientCallbacks()
  );


  if (!client->connect(footDevice)) {

    Serial.println("Connection failed.");

    return false;
  }


  Serial.println("BLE connection established.");


  BLERemoteService *service =
    client->getService(serviceUUID);


  if (service == nullptr) {

    Serial.println("ERROR: Service not found.");

    client->disconnect();

    return false;
  }


  remoteCharacteristic =
    service->getCharacteristic(charUUID);


  if (remoteCharacteristic == nullptr) {

    Serial.println(
      "ERROR: Characteristic not found."
    );

    client->disconnect();

    return false;
  }


  if (remoteCharacteristic->canNotify()) {

    remoteCharacteristic->registerForNotify(
      notifyCallback
    );

    Serial.println(
      "Subscribed to Foot IMU notifications."
    );

  } else {

    Serial.println(
      "ERROR: Characteristic cannot notify."
    );

    return false;
  }


  connected = true;

  return true;
}


// ======================================================
// Scan callback
// ======================================================

class ScanCallbacks :
  public BLEAdvertisedDeviceCallbacks {

  void onResult(BLEAdvertisedDevice device) {

    if (
      device.haveServiceUUID() &&
      device.isAdvertisingService(serviceUUID)
    ) {

      Serial.println("StrideGuard Foot found!");

      BLEDevice::getScan()->stop();

      footDevice =
        new BLEAdvertisedDevice(device);

      doConnect = true;
    }
  }
};


// ======================================================
// Setup
// ======================================================

void setup() {

  Serial.begin(115200);

  delay(2000);

  Serial.println(
    "Starting StrideGuard Wrist..."
  );


  // ====================================================
  // Wrist I2C
  // ====================================================

  Wire.begin();


  // ====================================================
  // Wrist IMU
  // ====================================================

  if (!wristIMU.begin_I2C(0x6A, &Wire)) {

    Serial.println(
      "ERROR: Wrist IMU not found!"
    );

    while (1) {
      delay(10);
    }
  }

  Serial.println("Wrist IMU found!");


  // ====================================================
  // DRV2605L
  // ====================================================

  if (!drv.begin()) {

    Serial.println(
      "ERROR: DRV2605L not found!"
    );

    while (1) {
      delay(10);
    }
  }

  drv.useERM();

  drv.selectLibrary(1);

  drv.setMode(
    DRV2605_MODE_INTTRIG
  );

  Serial.println("Haptic driver ready!");


  // ====================================================
  // BLE
  // ====================================================

  BLEDevice::init(
    "StrideGuard_Wrist"
  );

  BLEScan *scan =
    BLEDevice::getScan();

  scan->setAdvertisedDeviceCallbacks(
    new ScanCallbacks()
  );

  scan->setActiveScan(true);


  Serial.println(
    "Scanning for Foot Node..."
  );

  scan->start(5, false);
}


// ======================================================
// Main loop
// ======================================================

void loop() {

  // ---------- BLE connection ----------
  if (doConnect) {

    if (connectToFoot()) {

      Serial.println(
        "StrideGuard wireless link ready!"
      );

    } else {

      Serial.println(
        "Could not connect to Foot."
      );
    }

    doConnect = false;
  }


  // ====================================================
  // New Foot packet received
  // ====================================================

  if (newFootPacket) {

    newFootPacket = false;


    // Convert Foot data back to float
    float fax =
      latestFootPacket.ax / 100.0f;

    float fay =
      latestFootPacket.ay / 100.0f;

    float faz =
      latestFootPacket.az / 100.0f;

    float fgx =
      latestFootPacket.gx / 100.0f;

    float fgy =
      latestFootPacket.gy / 100.0f;

    float fgz =
      latestFootPacket.gz / 100.0f;


    // ---------- Read Wrist IMU ----------
    sensors_event_t wristAccel;
    sensors_event_t wristGyro;
    sensors_event_t wristTemp;

    wristIMU.getEvent(
      &wristAccel,
      &wristGyro,
      &wristTemp
    );


    // ==================================================
    // Print FOOT
    // ==================================================

    Serial.print("FOOT  t=");
    Serial.print(
      latestFootPacket.timestamp
    );

    Serial.print(" | A: ");

    Serial.print(fax);
    Serial.print(", ");

    Serial.print(fay);
    Serial.print(", ");

    Serial.print(faz);


    Serial.print(" | G: ");

    Serial.print(fgx);
    Serial.print(", ");

    Serial.print(fgy);
    Serial.print(", ");

    Serial.println(fgz);


    // ==================================================
    // Print WRIST
    // ==================================================

    Serial.print("WRIST     | A: ");

    Serial.print(
      wristAccel.acceleration.x
    );

    Serial.print(", ");

    Serial.print(
      wristAccel.acceleration.y
    );

    Serial.print(", ");

    Serial.print(
      wristAccel.acceleration.z
    );


    Serial.print(" | G: ");

    Serial.print(
      wristGyro.gyro.x
    );

    Serial.print(", ");

    Serial.print(
      wristGyro.gyro.y
    );

    Serial.print(", ");

    Serial.println(
      wristGyro.gyro.z
    );


    Serial.println("--------------------------");
  }


  // ====================================================
  // Manual vibration test
  // Type v in Serial Monitor and press Enter
  // ====================================================

  if (Serial.available()) {

    char command =
      Serial.read();

    if (command == 'v' ||
        command == 'V') {

      Serial.println(
        "HAPTIC FEEDBACK!"
      );

      drv.setWaveform(0, 1);
      drv.setWaveform(1, 0);

      drv.go();
    }
  }


  delay(5);
}