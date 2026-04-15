#include "Shared.h"

// Pins
const int LED_PIN = 2;
const int PUMP_PIN = 15;
const int LED_PIN2 = 22;
const int BUTTON_PIN = 33;

// Modem pins
const int MODEM_RX = 16;
const int MODEM_TX = 17;
const int MODEM_PWRKEY = 32;

// Alert thresholds
const uint16_t TEMP_ALERT_THRESHOLD = 50;
const uint16_t SPEED_ALERT_THRESHOLD = 150;
const unsigned long PUMP_TIME_ALERT = 3600000;  // 1 hour
const unsigned long ALERT_COOLDOWN = 300000;    // 5 minutes

// Source tracking
DataSource srcLed = SRC_NONE;
DataSource srcPump = SRC_NONE;
DataSource srcLed2 = SRC_NONE;
DataSource srcTemp = SRC_NONE;
DataSource srcSpeed = SRC_NONE;

// Shared data
bool     coilLed = false;
bool     coilPump = false;
bool     coilLed2 = false;
uint16_t setTemp = 20;
uint16_t setSpeed = 100;
uint16_t actualTemp = 0;
uint16_t voltage = 230;
uint16_t counterVal = 0;

// Alert tracking
unsigned long lastTempAlert = 0;
unsigned long lastSpeedAlert = 0;
unsigned long lastPumpAlert = 0;
unsigned long lastPumpOnAlert = 0;
unsigned long lastPumpOffAlert = 0;
unsigned long pumpOnStart = 0;
bool pumpWasOn = false;

// Timing
const unsigned long LOOP_INTERVAL_MS = 10;
const unsigned long DHCP_RENEW_MS = 60000;
const unsigned long BUTTON_DEBOUNCE_MS = 100;
unsigned long lastLoopTime = 0;
unsigned long lastDHCPCheck = 0;
bool apModeActive = false;
SemaphoreHandle_t stateMutex = nullptr;
SemaphoreHandle_t filesystemMutex = nullptr;

// Utilities
void Shared_init() {
  if (stateMutex == nullptr) {
    stateMutex = xSemaphoreCreateMutex();
  }

  if (filesystemMutex == nullptr) {
    filesystemMutex = xSemaphoreCreateMutex();
  }
}

bool Shared_lockState(TickType_t timeout) {
  return stateMutex != nullptr && xSemaphoreTake(stateMutex, timeout) == pdTRUE;
}

void Shared_unlockState() {
  if (stateMutex != nullptr) {
    xSemaphoreGive(stateMutex);
  }
}

bool Shared_lockFileSystem(TickType_t timeout) {
  return filesystemMutex != nullptr && xSemaphoreTake(filesystemMutex, timeout) == pdTRUE;
}

void Shared_unlockFileSystem() {
  if (filesystemMutex != nullptr) {
    xSemaphoreGive(filesystemMutex);
  }
}

SystemState Shared_getSnapshot() {
  SystemState snapshot = {};

  if (!Shared_lockState()) {
    return snapshot;
  }

  snapshot.srcLed = srcLed;
  snapshot.srcPump = srcPump;
  snapshot.srcLed2 = srcLed2;
  snapshot.srcTemp = srcTemp;
  snapshot.srcSpeed = srcSpeed;
  snapshot.coilLed = coilLed;
  snapshot.coilPump = coilPump;
  snapshot.coilLed2 = coilLed2;
  snapshot.setTemp = setTemp;
  snapshot.setSpeed = setSpeed;
  snapshot.actualTemp = actualTemp;
  snapshot.voltage = voltage;
  snapshot.counterVal = counterVal;
  snapshot.apModeActive = apModeActive;

  Shared_unlockState();
  return snapshot;
}

bool Shared_isAPModeActive() {
  bool active = false;

  if (Shared_lockState()) {
    active = apModeActive;
    Shared_unlockState();
  }

  return active;
}

void Shared_setAPModeActive(bool active) {
  if (Shared_lockState()) {
    apModeActive = active;
    Shared_unlockState();
  }
}

void applyHardware() {
  if (!Shared_lockState()) {
    return;
  }

  digitalWrite(LED_PIN,  coilLed  ? HIGH : LOW);
  digitalWrite(PUMP_PIN, coilPump ? HIGH : LOW);
  digitalWrite(LED_PIN2, coilLed2 ? HIGH : LOW);

  Shared_unlockState();
}

void updateSimulatedMetrics() {
  if (!Shared_lockState()) {
    return;
  }

  counterVal = millis() / 1000;
  actualTemp = setTemp + (millis() % 5);
  voltage    = 220 + (millis() % 10);

  Shared_unlockState();
}
