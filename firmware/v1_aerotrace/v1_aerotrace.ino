/*
 * - this is the bench testing firmware to stream data to our HTML dashboard over BLE.
 * - keeping LoRa wired but asleep (CS HIGH) so it doesn't inject noise during testing.
 * - using snprintf for the JSON payload to avoid memory leaks from String concatenation.
 */

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <HardwareSerial.h>
#include <SHT1x.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define DEVICE_NAME "AeroTrace_Prototype"
#define FIRMWARE_VERSION "1.0"

// standard nordic UART UUIDs so web bluetooth can find us easily
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// --- hardware pins ---
// PMS5003 optical sensor (UART2)
#define PMS_RX 16
#define PMS_TX 17

// SHT10 climate sensor
#define SHT_DATA_PIN 21
#define SHT_SCK_PIN 22

// analog gas sensors (using ADC1 pins so wifi/ble doesn't block them)
#define MQ7_PIN 33
#define MQ135_PIN 32

// LoRa SX1278 chip select
#define LORA_NSS_PIN 18 

// --- timers ---
const uint32_t TELEMETRY_INTERVAL = 400; // push json every 400ms (2.5 Hz)
const uint32_t SHT_INTERVAL = 2000;      // read temp/rh every 2s
const uint32_t PMS_TIMEOUT = 5000;       // flag as stale if no data for 5s

HardwareSerial pmsSerial(2);
SHT1x sht10(SHT_DATA_PIN, SHT_SCK_PIN);

BLEServer *pServer = nullptr;
BLECharacteristic *pTxCharacteristic = nullptr;
volatile bool deviceConnected = false;
bool advertisingRestartRequired = false;

uint32_t lastTelemetryTime = 0;
uint32_t lastShtTime = 0;
uint32_t packetSequence = 0;
char jsonBuffer[256];

// dump all sensor readings here so they are easy to pack into JSON later
struct SensorData {
  uint16_t pm1 = 0;
  uint16_t pm25 = 0;
  uint16_t pm10 = 0;
  float temperature = NAN;
  float humidity = NAN;
  uint16_t mq7Raw = 0;
  uint16_t mq135Raw = 0;

  bool pmsValid = false;
  bool temperatureValid = false;
  bool humidityValid = false;
  uint32_t lastPmsUpdate = 0;
  uint32_t lastShtUpdate = 0;
};
SensorData sensor;


// --- PMS5003 parser ---
uint8_t pmsFrame[32];
uint8_t pmsIndex = 0;

bool processPmsFrame() {
  // check for standard 0x42 0x4D header
  if (pmsFrame[0] != 0x42 || pmsFrame[1] != 0x4D) return false;

  // length should be 28 bytes
  uint16_t frameLength = ((uint16_t)pmsFrame[2] << 8) | pmsFrame[3];
  if (frameLength != 28) return false;

  // sum bytes 0-29 to verify checksum against bytes 30-31
  uint16_t checksum = 0;
  for (int i = 0; i < 30; i++) {
    checksum += pmsFrame[i];
  }
  uint16_t receivedChecksum = ((uint16_t)pmsFrame[30] << 8) | pmsFrame[31];
  if (checksum != receivedChecksum) return false;

  // extract atmospheric PM data
  sensor.pm1 = ((uint16_t)pmsFrame[10] << 8) | pmsFrame[11];
  sensor.pm25 = ((uint16_t)pmsFrame[12] << 8) | pmsFrame[13];
  sensor.pm10 = ((uint16_t)pmsFrame[14] << 8) | pmsFrame[15];

  sensor.pmsValid = true;
  sensor.lastPmsUpdate = millis();
  return true;
}

void readPMS5003() {
  // read whatever is sitting in the hardware serial buffer
  while (pmsSerial.available() > 0) {
    uint8_t incomingByte = pmsSerial.read();

    if (pmsIndex == 0) {
      if (incomingByte == 0x42) pmsFrame[pmsIndex++] = incomingByte;
      continue;
    }
    if (pmsIndex == 1) {
      if (incomingByte == 0x4D) pmsFrame[pmsIndex++] = incomingByte;
      else if (incomingByte != 0x42) pmsIndex = 0;
      continue;
    }

    pmsFrame[pmsIndex++] = incomingByte;

    // process full 32-byte frame and reset
    if (pmsIndex >= sizeof(pmsFrame)) {
      processPmsFrame();
      pmsIndex = 0;
    }
  }
}


// --- climate & gas sensors ---
void readSHT10() {
  float temperature = sht10.readTemperatureC();
  float humidity = sht10.readHumidity();

  // ignore garbage readings if the wire gets bumped
  bool tempOK = isfinite(temperature) && temperature >= -40.0 && temperature <= 125.0;
  bool humidityOK = isfinite(humidity) && humidity >= 0.0 && humidity <= 100.0;

  sensor.temperatureValid = tempOK;
  if (tempOK) sensor.temperature = temperature;

  sensor.humidityValid = humidityOK;
  if (humidityOK) sensor.humidity = humidity;

  if (tempOK || humidityOK) sensor.lastShtUpdate = millis();
}

void readGasSensors() {
  sensor.mq7Raw = analogRead(MQ7_PIN);
  sensor.mq135Raw = analogRead(MQ135_PIN);
}

void checkStaleData() {
  uint32_t now = millis();
  // mark sensors invalid if they stop responding
  if (sensor.pmsValid && now - sensor.lastPmsUpdate > PMS_TIMEOUT) {
    sensor.pmsValid = false;
  }
  if (now - sensor.lastShtUpdate > 10000) {
    sensor.temperatureValid = false;
    sensor.humidityValid = false;
  }
}


// --- telemetry ---
void generateJSON() {
  packetSequence++;
  checkStaleData();

  char pm1Text[12], pm25Text[12], pm10Text[12];
  char tempText[20], humidityText[20];

  // send 'null' to the dashboard if a sensor drops out instead of fake 0s
  if (sensor.pmsValid) {
    snprintf(pm1Text, sizeof(pm1Text), "%u", sensor.pm1);
    snprintf(pm25Text, sizeof(pm25Text), "%u", sensor.pm25);
    snprintf(pm10Text, sizeof(pm10Text), "%u", sensor.pm10);
  } else {
    snprintf(pm1Text, sizeof(pm1Text), "null");
    snprintf(pm25Text, sizeof(pm25Text), "null");
    snprintf(pm10Text, sizeof(pm10Text), "null");
  }

  if (sensor.temperatureValid) snprintf(tempText, sizeof(tempText), "%.1f", sensor.temperature);
  else snprintf(tempText, sizeof(tempText), "null");

  if (sensor.humidityValid) snprintf(humidityText, sizeof(humidityText), "%.1f", sensor.humidity);
  else snprintf(humidityText, sizeof(humidityText), "null");

  // build json
  snprintf(
    jsonBuffer, sizeof(jsonBuffer),
    "{"
      "\"device\":\"%s\","
      "\"version\":\"%s\","
      "\"seq\":%lu,"
      "\"uptime_ms\":%lu,"
      "\"pm1\":%s,"
      "\"pm25\":%s,"
      "\"pm10\":%s,"
      "\"temp\":%s,"
      "\"rh\":%s,"
      "\"mq7_raw\":%u,"
      "\"mq135_raw\":%u,"
      "\"sensor_status\":{"
        "\"pms5003\":%s,"
        "\"sht10_temp\":%s,"
        "\"sht10_rh\":%s,"
        "\"mq7\":true,"
        "\"mq135\":true"
      "}"
    "}",
    DEVICE_NAME, FIRMWARE_VERSION,
    (unsigned long)packetSequence, (unsigned long)millis(),
    pm1Text, pm25Text, pm10Text, tempText, humidityText,
    sensor.mq7Raw, sensor.mq135Raw,
    sensor.pmsValid ? "true" : "false",
    sensor.temperatureValid ? "true" : "false",
    sensor.humidityValid ? "true" : "false"
  );
}


// --- ble setup ---
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
    Serial.println("[BLE] dashboard connected");
  }
  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    advertisingRestartRequired = true;
    Serial.println("[BLE] dashboard dropped. restarting advertising");
  }
};

void initializeBLE() {
  BLEDevice::init(DEVICE_NAME);
  // request larger MTU so the json string doesn't get clipped
  BLEDevice::setMTU(185);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void transmitTelemetry() {
  if (!deviceConnected) return;
  generateJSON();

  size_t payloadLength = strlen(jsonBuffer);
  // prevent stack overflow if string gets unexpectedly huge
  if (payloadLength > 182) {
    Serial.println("[BLE] payload too big, skipping");
    return;
  }

  pTxCharacteristic->setValue((uint8_t *)jsonBuffer, payloadLength);
  pTxCharacteristic->notify();
  Serial.print("[TX] ");
  Serial.println(jsonBuffer);
}


// --- main loop ---
void setup() {
  Serial.begin(115200);
  delay(500);

  // lock LoRa in standby so it doesn't wake up and pollute our power rails
  pinMode(LORA_NSS_PIN, OUTPUT);
  digitalWrite(LORA_NSS_PIN, HIGH);

  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);

  // setup ADCs
  analogReadResolution(12);
  analogSetPinAttenuation(MQ7_PIN, ADC_11db);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);
  pinMode(MQ7_PIN, INPUT);
  pinMode(MQ135_PIN, INPUT);

  initializeBLE();
  Serial.println("[SYSTEM] boot complete. running loop");
}

void loop() {
  uint32_t currentTime = millis();

  // 1. read pms5003 constantly so we don't miss frames
  readPMS5003();

  // 2. bounce ble back up if client disconnects
  if (advertisingRestartRequired) {
    advertisingRestartRequired = false;
    delay(100);
    if (!deviceConnected) BLEDevice::startAdvertising();
  }

  // 3. poll climate sensor on its own timer
  if (currentTime - lastShtTime >= SHT_INTERVAL) {
    lastShtTime = currentTime;
    readSHT10();
  }

  readGasSensors();

  // 4. push data to dashboard
  if (currentTime - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = currentTime;
    transmitTelemetry();
  }
}
