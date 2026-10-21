#include <Arduino.h>
#include <BLEDevice.h>

// Must exactly match Foot
static BLEUUID serviceUUID(
  "12345678-1234-1234-1234-1234567890ab"
);

static BLEUUID charUUID(
  "abcdefab-1234-1234-1234-abcdefabcdef"
);

static bool doConnect = false;
static bool connected = false;

static BLERemoteCharacteristic *remoteCharacteristic;
static BLEAdvertisedDevice *footDevice;


// Must match Foot struct exactly
struct FootAccelData {
  float ax;
  float ay;
  float az;
};


// Called automatically whenever Foot sends data
static void notifyCallback(
  BLERemoteCharacteristic *characteristic,
  uint8_t *data,
  size_t length,
  bool isNotify
) {

  if (length != sizeof(FootAccelData)) {
    Serial.print("Unexpected packet size: ");
    Serial.println(length);
    return;
  }

  FootAccelData footData;

  memcpy(
    &footData,
    data,
    sizeof(FootAccelData)
  );

  Serial.print("Foot Accel X: ");
  Serial.print(footData.ax);

  Serial.print("  Y: ");
  Serial.print(footData.ay);

  Serial.print("  Z: ");
  Serial.println(footData.az);
}


// ---------- Connection callbacks ----------
class WristClientCallbacks : public BLEClientCallbacks {

  void onConnect(BLEClient *client) {
    Serial.println("Connected to Foot!");
  }

  void onDisconnect(BLEClient *client) {
    connected = false;
    Serial.println("Foot disconnected.");
  }
};


// ---------- Connect to Foot ----------
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

  Serial.println("StrideGuard service found.");

  remoteCharacteristic =
    service->getCharacteristic(charUUID);

  if (remoteCharacteristic == nullptr) {
    Serial.println("ERROR: Characteristic not found.");
    client->disconnect();
    return false;
  }

  Serial.println("StrideGuard characteristic found.");

  if (remoteCharacteristic->canNotify()) {

    remoteCharacteristic->registerForNotify(
      notifyCallback
    );

    Serial.println(
      "Subscribed to Foot notifications."
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


// ---------- BLE scan ----------
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


void setup() {

  Serial.begin(115200);
  delay(2000);

  Serial.println(
    "Starting Wrist BLE IMU receiver..."
  );

  BLEDevice::init("StrideGuard_Wrist");

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


void loop() {

  if (doConnect) {

    if (connectToFoot()) {

      Serial.println(
        "Wrist receiving Foot IMU data!"
      );

    } else {

      Serial.println(
        "Could not connect."
      );
    }

    doConnect = false;
  }

  delay(100);
}