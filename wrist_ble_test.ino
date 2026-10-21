#include <Arduino.h>
#include <BLEDevice.h>

// Must exactly match the Foot Node
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


// Called every time Foot sends a notification
static void notifyCallback(
  BLERemoteCharacteristic *characteristic,
  uint8_t *data,
  size_t length,
  bool isNotify
) {
  Serial.print("Received from Foot: ");

  for (size_t i = 0; i < length; i++) {
    Serial.print((char)data[i]);
  }

  Serial.println();
}


// Connection callbacks
class WristClientCallbacks : public BLEClientCallbacks {

  void onConnect(BLEClient *client) {
    Serial.println("Connected to Foot!");
  }

  void onDisconnect(BLEClient *client) {
    connected = false;
    Serial.println("Foot disconnected.");
  }
};


// Connect to Foot
bool connectToFoot() {

  Serial.println("Connecting to StrideGuard Foot...");

  BLEClient *client = BLEDevice::createClient();
  client->setClientCallbacks(new WristClientCallbacks());

  if (!client->connect(footDevice)) {
    Serial.println("Connection failed.");
    return false;
  }

  Serial.println("BLE connection established.");

  // Find our custom service
  BLERemoteService *service =
    client->getService(serviceUUID);

  if (service == nullptr) {
    Serial.println("ERROR: Service not found.");
    client->disconnect();
    return false;
  }

  Serial.println("StrideGuard service found.");

  // Find our custom characteristic
  remoteCharacteristic =
    service->getCharacteristic(charUUID);

  if (remoteCharacteristic == nullptr) {
    Serial.println("ERROR: Characteristic not found.");
    client->disconnect();
    return false;
  }

  Serial.println("StrideGuard characteristic found.");

  // Subscribe to notifications from Foot
  if (remoteCharacteristic->canNotify()) {
    remoteCharacteristic->registerForNotify(
      notifyCallback
    );

    Serial.println("Subscribed to Foot notifications.");
  } else {
    Serial.println("ERROR: Characteristic cannot notify.");
    return false;
  }

  connected = true;
  return true;
}


// Called for every BLE device the Wrist discovers
class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {

  void onResult(BLEAdvertisedDevice device) {

    if (
      device.haveServiceUUID() &&
      device.isAdvertisingService(serviceUUID)
    ) {

      Serial.println("StrideGuard Foot found!");

      BLEDevice::getScan()->stop();

      footDevice = new BLEAdvertisedDevice(device);

      doConnect = true;
    }
  }
};


void setup() {

  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting Wrist BLE client...");

  BLEDevice::init("StrideGuard_Wrist");

  BLEScan *scan = BLEDevice::getScan();

  scan->setAdvertisedDeviceCallbacks(
    new ScanCallbacks()
  );

  scan->setActiveScan(true);

  Serial.println("Scanning for Foot Node...");

  // Scan for 5 seconds
  scan->start(5, false);
}


void loop() {

  if (doConnect) {

    if (connectToFoot()) {
      Serial.println("Wrist is ready to receive data!");
    } else {
      Serial.println("Could not connect.");
    }

    doConnect = false;
  }

  // Notifications arrive automatically
  delay(100);
}