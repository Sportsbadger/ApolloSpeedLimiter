/* Apollo Phantom V4X: configurable speed-limit request test.
   Automatically runs once after power-up. Test only while stationary.
   A matching notification is not proof of actual speed enforcement. */
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <cstring>

// Set the desired whole-number speed in mph. Use 8, not 08 (C++ syntax).
constexpr unsigned DESIRED_TOP_SPEED_MPH = 10;
static_assert(DESIRED_TOP_SPEED_MPH >= 1 && DESIRED_TOP_SPEED_MPH <= 99,
              "Set a whole-number speed from 1 to 99 mph.");
// The observed 10 mph command uses 16 km/h, encoded as 160. Other values
// use the same inferred conversion; supported scooter limits are not known.
constexpr uint16_t SPEED_REGISTER = ((DESIRED_TOP_SPEED_MPH * 160934UL + 50000UL) / 100000UL) * 10;
constexpr uint32_t SCAN_MS = 7000;
constexpr uint32_t BETWEEN_COMMANDS_MS = 30;
constexpr uint32_t ACK_WAIT_MS = 450;
constexpr unsigned MAX_ATTEMPTS = 5;
const char* TARGET_BLE_ADDRESS = "cd:00:01:03:66:80";
const NimBLEUUID CONTROL_SERVICE("F1F0"), WRITE_UUID("F1F1"), NOTIFY_UUID("F1F2");
const uint8_t PREPARE[] = {0xA5, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x5A};
uint8_t speedFrame[] = {0x01, 0x17, 0x00, 0x20, 0x00, 0x01, 0x00, 0x20, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00};
uint8_t expectedAck[] = {0x01, 0x17, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00};
std::atomic<bool> gotAck{false};

void finishFrame(uint8_t* frame, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length - 2; ++i) {
    crc ^= frame[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  frame[length - 2] = crc & 0xFF;
  frame[length - 1] = crc >> 8;
}

void prepareFrames() {
  speedFrame[11] = expectedAck[5] = SPEED_REGISTER >> 8;
  speedFrame[12] = expectedAck[6] = SPEED_REGISTER & 0xFF;
  finishFrame(speedFrame, sizeof(speedFrame));
  finishFrame(expectedAck, sizeof(expectedAck));
}

class ScanDiagnostics : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    Serial.printf("  Name: %s | MAC: %s | RSSI: %d dBm\n",
                  device->haveName() ? device->getName().c_str() : "<unnamed>",
                  device->getAddress().toString().c_str(), device->getRSSI());
  }
};
ScanDiagnostics scanDiagnostics;

void onNotification(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  Serial.print("F1F2: ");
  for (size_t i = 0; i < length; ++i) Serial.printf("%02X%s", data[i], i + 1 < length ? " " : "\n");
  if (length >= sizeof(expectedAck) && memcmp(data, expectedAck, sizeof(expectedAck)) == 0) gotAck.store(true);
}

void runTest() {
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
    Serial.println("ERROR: scooter not found.");
    scan->clearResults();
    return;
  }
  NimBLEClient* client = NimBLEDevice::createClient();
  if (!client) {
    Serial.println("ERROR: BLE client creation failed.");
    scan->clearResults();
    return;
  }
  client->setConnectTimeout(5000);
  bool connected = client->connect(target);
  scan->clearResults();
  if (!connected) {
    Serial.println("ERROR: connection failed.");
  } else {
    NimBLERemoteService* service = client->getService(CONTROL_SERVICE);
    NimBLERemoteCharacteristic* writer = service ? service->getCharacteristic(WRITE_UUID) : nullptr;
    NimBLERemoteCharacteristic* receiver = service ? service->getCharacteristic(NOTIFY_UUID) : nullptr;
    if (!writer || !writer->canWriteNoResponse() || !receiver || !receiver->canNotify() ||
        !receiver->subscribe(true, onNotification)) {
      Serial.println("ERROR: required write or notification characteristic unavailable.");
    } else {
      Serial.printf("Connected; requesting %u mph (%u km/h encoded) up to five times.\n",
                    DESIRED_TOP_SPEED_MPH, SPEED_REGISTER / 10);
      for (unsigned attempt = 1; attempt <= MAX_ATTEMPTS && client->isConnected() && !gotAck.load(); ++attempt) {
        Serial.printf("Attempt %u/%u\n", attempt, MAX_ATTEMPTS);
        if (!writer->writeValue(PREPARE, sizeof(PREPARE), false)) {
          Serial.println("ERROR: preparation write failed.");
          break;
        }
        delay(BETWEEN_COMMANDS_MS);
        if (!writer->writeValue(speedFrame, sizeof(speedFrame), false)) {
          Serial.println("ERROR: speed write failed.");
          break;
        }
        uint32_t start = millis();
        while (!gotAck.load() && client->isConnected() && millis() - start < ACK_WAIT_MS) delay(10);
      }
      if (gotAck.load()) Serial.println("Matching speed response received; actual speed enforcement is not confirmed.");
      else Serial.println("No matching response; setting remains unconfirmed.");
    }
  }
  if (client->isConnected()) client->disconnect();
  NimBLEDevice::deleteClient(client);
  Serial.println("Disconnected. Test finished; reset ESP32 to repeat.");
}

void setup() {
  Serial.begin(115200);
  prepareFrames();
  NimBLEDevice::init("PhantomV4X-SpeedTest");
  NimBLEDevice::setMTU(128);
  Serial.println("Stationary test only. Disconnect Apollo and nRF Connect.");
  delay(3000);
  runTest();
}
void loop() { delay(1000); }
