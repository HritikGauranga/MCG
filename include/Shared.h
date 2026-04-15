#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

typedef enum { SRC_NONE, SRC_RTU, SRC_TCP } DataSource;

struct SystemState {
  DataSource srcLed;
  DataSource srcPump;
  DataSource srcLed2;
  DataSource srcTemp;
  DataSource srcSpeed;

  bool coilLed;
  bool coilPump;
  bool coilLed2;
  uint16_t setTemp;
  uint16_t setSpeed;
  uint16_t actualTemp;
  uint16_t voltage;
  uint16_t counterVal;
  bool apModeActive;
};

// Pins
extern const int LED_PIN;
extern const int PUMP_PIN;
extern const int LED_PIN2;
extern const int BUTTON_PIN;

// Modem pins
extern const int MODEM_RX;
extern const int MODEM_TX;
extern const int MODEM_PWRKEY;

// Alert thresholds
extern const uint16_t TEMP_ALERT_THRESHOLD;
extern const uint16_t SPEED_ALERT_THRESHOLD;
extern const unsigned long PUMP_TIME_ALERT;
extern const unsigned long ALERT_COOLDOWN;

// Source tracking
extern DataSource srcLed;
extern DataSource srcPump;
extern DataSource srcLed2;
extern DataSource srcTemp;
extern DataSource srcSpeed;

// Shared data
extern bool     coilLed;
extern bool     coilPump;
extern bool     coilLed2;
extern uint16_t setTemp;
extern uint16_t setSpeed;
extern uint16_t actualTemp;
extern uint16_t voltage;
extern uint16_t counterVal;

// Alert tracking
extern unsigned long lastTempAlert;
extern unsigned long lastSpeedAlert;
extern unsigned long lastPumpAlert;
extern unsigned long lastPumpOnAlert;
extern unsigned long lastPumpOffAlert;
extern unsigned long pumpOnStart;
extern bool pumpWasOn;

// Timing
extern const unsigned long LOOP_INTERVAL_MS;
extern const unsigned long DHCP_RENEW_MS;
extern const unsigned long BUTTON_DEBOUNCE_MS;
extern unsigned long lastLoopTime;
extern unsigned long lastDHCPCheck;
extern bool apModeActive;

extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t filesystemMutex;

// Utilities
void Shared_init();
bool Shared_lockState(TickType_t timeout = pdMS_TO_TICKS(50));
void Shared_unlockState();
bool Shared_lockFileSystem(TickType_t timeout = pdMS_TO_TICKS(500));
void Shared_unlockFileSystem();
SystemState Shared_getSnapshot();
bool Shared_isAPModeActive();
void Shared_setAPModeActive(bool active);
void applyHardware();
void updateSimulatedMetrics();
