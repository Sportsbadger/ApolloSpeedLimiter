/*
  Apollo Phantom V4X — stationary Eco / Comfort BLE test
  Automatically connects and alternates requested modes every 10 seconds.
  Test only with the scooter stationary and nobody riding it.
*/
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>

constexpr uint8_t LED_PIN = 19;
constexpr uint32_t SCAN_MS = 7000;
constexpr uint32_t MODE_INTERVAL_MS = 10000;
constexpr uint32_t RETRY_MS = 3000;
const char* TARGET_BLE_ADDRESS = "cd:00:01:03:66:80";
const NimBLEUUID CONTROL_SERVICE("F1F0");
const NimBLEUUID WRITE_UUID("F1F1");
const NimBLEUUID NOTIFY_UUID("F1F2");
const uint8_t COMFORT_FRAME[] = {0xAB, 0x00, 0x0A, 0xF1, 0x03, 0x23, 0x30, 0x40, 0x92, 0xC1};
const uint8_t ECO_FRAME[] = {0xAB, 0x00, 0x0A, 0xF0, 0x03, 0x23, 0x30, 0x40, 0xAF, 0x01};

std::atomic<int> reportedMode{-1};
NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* writer = nullptr;
bool requestComfort = true;
uint32_t nextAction = 0;

class ScanDiagnostics : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    Serial.printf("  Name: %s | MAC: %s | RSSI: %d dBm\n",
                  device->haveName() ? device->getName().c_str() : "<unnamed>",
                  device->getAddress().toString().c_str(), device->getRSSI());
  }
};
ScanDiagnostics scanDiagnostics;

void onNotification(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  // Captures show byte 4 of AB 00 19 01 status packets as 0=Eco, 1=Comfort.
  if (length >= 5 && data[0] == 0xAB && data[1] == 0x00 &&
      data[2] == 0x19 && data[3] == 0x01 && data[4] <= 1) {
    int previous = reportedMode.exchange(data[4]);
    if (previous != data[4]) {
      Serial.printf("Scooter reports %s mode.\n", data[4] ? "Comfort" : "Eco");
    }
  }
}

void disconnectClient() {
  writer = nullptr;
  reportedMode.store(-1);
  digitalWrite(LED_PIN, LOW);
  if (client) {
    if (client->isConnected()) client->disconnect();
    NimBLEDevice::deleteClient(client);
    client = nullptr;
  }
}

bool connectScooter() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->setMaxResults(255);
  scan->setScanCallbacks(&scanDiagnostics);
  Serial.println("Scanning for scooter...");
  NimBLEScanResults results = scan->getResults(SCAN_MS, false);
  const NimBLEAdvertisedDevice* target = nullptr;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (device && device->getAddress().toString() == std::string(TARGET_BLE_ADDRESS)) {
      target = device;
      break;
    }
  }
  if (!target) {
    Serial.println("Scooter not found; retrying.");
    scan->clearResults();
    return false;
  }
  client = NimBLEDevice::createClient();
  if (!client) {
    Serial.println("Could not create BLE client.");
    scan->clearResults();
    return false;
  }
  client->setConnectTimeout(5000);
  bool connected = client->connect(target);
  scan->clearResults();
  if (!connected) {
    Serial.println("Connection failed.");
    disconnectClient();
    return false;
  }
  NimBLERemoteService* service = client->getService(CONTROL_SERVICE);
  writer = service ? service->getCharacteristic(WRITE_UUID) : nullptr;
  NimBLERemoteCharacteristic* receiver = service ? service->getCharacteristic(NOTIFY_UUID) : nullptr;
  if (!writer || !writer->canWrite() || !receiver || !receiver->canNotify() ||
      !receiver->subscribe(true, onNotification)) {
    Serial.println("Required write or notification characteristic unavailable.");
    disconnectClient();
    return false;
  }
  Serial.println("Connected; notifications enabled. First request: Comfort.");
  digitalWrite(LED_PIN, HIGH);
  requestComfort = true;
  nextAction = millis();
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  NimBLEDevice::init("PhantomV4X-ModeTest");
  NimBLEDevice::setMTU(128);
  Serial.println("Eco / Comfort test: scooter must be stationary; nobody riding.");
  Serial.println("Close Apollo and nRF Connect before starting.");
  delay(3000);
}

void loop() {
  if (!client || !client->isConnected()) {
    if (client) {
      Serial.println("Disconnected; reconnecting.");
      disconnectClient();
    }
    if (!connectScooter()) delay(RETRY_MS);
    return;
  }
  if (static_cast<int32_t>(millis() - nextAction) >= 0) {
    const uint8_t* frame = requestComfort ? COMFORT_FRAME : ECO_FRAME;
    Serial.printf("Requesting %s mode (write with response)...\n", requestComfort ? "Comfort" : "Eco");
    if (writer->writeValue(frame, sizeof(COMFORT_FRAME), true)) {
      Serial.println("Write accepted; watch for scooter status and verify on display.");
      requestComfort = !requestComfort;
    } else {
      Serial.println("Write failed; will retry the same mode.");
    }
    nextAction = millis() + MODE_INTERVAL_MS;
  }
  delay(20);
}
