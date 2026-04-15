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
bool pumpWereOn = false;
bool pumpWasOn = false;

// Previous values (RTU)
bool     prevCoilLed_RTU = false;
bool     prevCoilPump_RTU = false;
bool     prevCoilLed2_RTU = false;
uint16_t prevSetTemp_RTU = 20;
uint16_t prevSetSpeed_RTU = 100;

// Previous values (TCP)
bool     prevCoilLed_TCP = false;
bool     prevCoilPump_TCP = false;
bool     prevCoilLed2_TCP = false;
uint16_t prevSetTemp_TCP = 20;
uint16_t prevSetSpeed_TCP = 100;

// Timing
const unsigned long LOOP_INTERVAL_MS = 10;
const unsigned long DHCP_RENEW_MS = 60000;
const unsigned long BUTTON_DEBOUNCE_MS = 100;
unsigned long lastLoopTime = 0;
unsigned long lastDHCPCheck = 0;
unsigned long lastButtonChange = 0;
bool apModeActive = false;
bool serverRoutesSetup = false;
bool lastButtonState = HIGH;

// Utilities
void applyHardware() {
  digitalWrite(LED_PIN,  coilLed  ? HIGH : LOW);
  digitalWrite(PUMP_PIN, coilPump ? HIGH : LOW);
  digitalWrite(LED_PIN2, coilLed2 ? HIGH : LOW);
}

void updateSimulatedMetrics() {
  counterVal = millis() / 1000;
  actualTemp = setTemp + (millis() % 5);
  voltage    = 220 + (millis() % 10);
}
