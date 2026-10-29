/*
  Apollo Phantom V4X — automatic Bluetooth speed-limit controller

  ESP32 firmware using NimBLE-Arduino 2.x. On power-up it finds the scooter
  by its BLE address, connects, and requests the configured top speed. It
  retries until the scooter reports the requested value, then disconnects.
  Reset or power-cycle the ESP32 to run the sequence again.

  Configure SPEED_UNIT and DESIRED_TOP_SPEED below, then build and upload.
  Close the Apollo app and other BLE clients before starting so the ESP32 can
  connect. Serial diagnostics are available at 115200 baud.

  The scooter has been observed to enforce a 10 mph setting and retain it
  after a power cycle. Other speeds and the conversion are not all verified.
  A matching BLE response reports a setting; it does not prove enforcement.
  Configure and verify the limit with the scooter stationary.
*/
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <cstring>

// User settings: select MPH or KMH and enter a whole number in that unit.
// Use 8, not 08: C++ treats a leading zero as octal notation.
enum class SpeedUnit { MPH, KMH };
constexpr SpeedUnit SPEED_UNIT = SpeedUnit::MPH; // Change to SpeedUnit::KMH for km/h.
constexpr unsigned DESIRED_TOP_SPEED = 20;
// This is a project configuration bound, not a scooter capability or safe speed.
static_assert(DESIRED_TOP_SPEED >= 1 && DESIRED_TOP_SPEED <= 50,
              "Set a whole-number speed from 1 to 50 in the selected unit.");
// Convert mph to the nearest whole km/h using integer arithmetic. A km/h
// setting is used directly. The observed 10 mph command uses 16 km/h encoded
// as 160 (tenths of km/h). Other values follow this inferred encoding.
constexpr uint16_t SPEED_KMH = SPEED_UNIT == SpeedUnit::MPH
    ? (DESIRED_TOP_SPEED * 160934UL + 50000UL) / 100000UL
    : DESIRED_TOP_SPEED;
constexpr uint16_t SPEED_REGISTER = SPEED_KMH * 10;
// Maximum scan duration; scanning stops earlier when the target is discovered.
constexpr uint32_t SCAN_MS = 7000;
// The captured sequence uses a 30 ms gap between the two writes.
constexpr uint32_t BETWEEN_COMMANDS_MS = 30;
// Give the scooter time to report the requested value before sending again.
constexpr uint32_t ACK_WAIT_MS = 450;
// Pause before restarting a scan after a connection, scan or write failure.
constexpr uint32_t RECONNECT_PAUSE_MS = 1000;
// Match the scooter by address because its advertised name has a trailing byte.
const char* TARGET_BLE_ADDRESS = "cd:00:01:03:66:80";
// F1F1 accepts commands; F1F2 publishes status and command responses.
const NimBLEUUID CONTROL_SERVICE("F1F0"), WRITE_UUID("F1F1"), NOTIFY_UUID("F1F2");
// Observed preparation frame. The speed request and expected reply below are
// templates: prepareFrames() fills their value bytes and Modbus CRC-16.
const uint8_t PREPARE[] = {0xA5, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x5A};
uint8_t speedFrame[] = {0x01, 0x17, 0x00, 0x20, 0x00, 0x01, 0x00, 0x20, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00};
uint8_t expectedAck[] = {0x01, 0x17, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00};
std::atomic<bool> gotAck{false};
std::atomic<bool> foundScooter{false};

// Append a Modbus CRC-16, least significant byte first, to either frame.
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
  // The speed value itself is stored most significant byte first; the CRC
  // appended afterward has the opposite byte order.
  speedFrame[11] = expectedAck[5] = SPEED_REGISTER >> 8;
  speedFrame[12] = expectedAck[6] = SPEED_REGISTER & 0xFF;
  finishFrame(speedFrame, sizeof(speedFrame));
  finishFrame(expectedAck, sizeof(expectedAck));
}

class ScanDiagnostics : public NimBLEScanCallbacks {
  // Discovery happens on the BLE callback task; signal the main task to stop
  // scanning as soon as the target is seen, without stopping it in the callback.
  void onDiscovered(const NimBLEAdvertisedDevice* device) override {
    if (device->getAddress().toString() == std::string(TARGET_BLE_ADDRESS))
      foundScooter.store(true);
  }
  void onResult(const NimBLEAdvertisedDevice* device) override {
    // Completed scan results may include a name from the scan response.
    Serial.printf("  Name: %s | MAC: %s | RSSI: %d dBm\n",
                  device->haveName() ? device->getName().c_str() : "<unnamed>",
                  device->getAddress().toString().c_str(), device->getRSSI());
  }
};
ScanDiagnostics scanDiagnostics;

void onNotification(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  // Print the raw notification for diagnosis, then compare it with the response
  // for the configured speed. BLE callbacks share flags with the main loop.
  Serial.print("F1F2: ");
  for (size_t i = 0; i < length; ++i) Serial.printf("%02X%s", data[i], i + 1 < length ? " " : "\n");
  if (length >= sizeof(expectedAck) && memcmp(data, expectedAck, sizeof(expectedAck)) == 0) gotAck.store(true);
}

void applySpeedLimit() {
  // Start with fresh results so an old advertisement cannot be selected.
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->setMaxResults(255);
  scan->setScanCallbacks(&scanDiagnostics);
  scan->clearResults();
  foundScooter.store(false);
  Serial.println("Scanning for scooter...");
  if (!scan->start(SCAN_MS, false)) {
    Serial.println("ERROR: scan could not start; retrying.");
    return;
  }
  while (scan->isScanning() && !foundScooter.load()) delay(10);
  if (scan->isScanning()) scan->stop();
  NimBLEScanResults results = scan->getResults();
  const NimBLEAdvertisedDevice* target = nullptr;
  // The results own the advertised device; keep them until connect returns.
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
    // Discover the service and subscribe before writing, so replies are seen.
    NimBLERemoteService* service = client->getService(CONTROL_SERVICE);
    NimBLERemoteCharacteristic* writer = service ? service->getCharacteristic(WRITE_UUID) : nullptr;
    NimBLERemoteCharacteristic* receiver = service ? service->getCharacteristic(NOTIFY_UUID) : nullptr;
    if (!writer || !writer->canWriteNoResponse() || !receiver || !receiver->canNotify() ||
        !receiver->subscribe(true, onNotification)) {
      Serial.println("ERROR: required write or notification characteristic unavailable.");
    } else {
      Serial.printf("Connected; requesting %u %s (%u km/h encoded) until acknowledged.\n",
                    DESIRED_TOP_SPEED, SPEED_UNIT == SpeedUnit::MPH ? "mph" : "km/h", SPEED_KMH);
      for (unsigned long attempt = 1; client->isConnected() && !gotAck.load(); ++attempt) {
        Serial.printf("Attempt %lu\n", attempt);
        if (!writer->writeValue(PREPARE, sizeof(PREPARE), false)) {
          Serial.println("ERROR: preparation write failed.");
          break;
        }
        // Keep this interval free of serial output and other deliberate work.
        delay(BETWEEN_COMMANDS_MS);
        if (!writer->writeValue(speedFrame, sizeof(speedFrame), false)) {
          Serial.println("ERROR: speed write failed.");
          break;
        }
        // Retry on the same connection if the expected reply doesn't arrive.
        // Unsigned subtraction remains valid across millis() wraparound.
        uint32_t start = millis();
        while (!gotAck.load() && client->isConnected() && millis() - start < ACK_WAIT_MS) delay(10);
      }
      if (gotAck.load()) Serial.println("Matching speed response received; actual speed enforcement is not confirmed.");
      else Serial.println("No matching response; setting remains unconfirmed.");
    }
  }
  // Always release the client; loop() will reconnect if no response was seen.
  if (client->isConnected()) client->disconnect();
  NimBLEDevice::deleteClient(client);
  Serial.println(gotAck.load() ? "Disconnected. Request finished." : "Disconnected. Will retry.");
}

void setup() {
  // Arduino calls setup once at boot; there is no deliberate startup delay.
  Serial.begin(115200);
  prepareFrames();
  NimBLEDevice::init("PhantomV4X-SpeedTest");
  NimBLEDevice::setMTU(128);
  Serial.println("Configure and verify while stationary. Disconnect Apollo and nRF Connect.");
}
void loop() {
  // Keep reconnecting after failures. Once acknowledged, stay idle until reset.
  if (!gotAck.load()) {
    applySpeedLimit();
    if (!gotAck.load()) delay(RECONNECT_PAUSE_MS);
  } else {
    delay(1000);
  }
}
