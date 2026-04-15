#pragma once
#include <Arduino.h>

typedef enum { SRC_NONE, SRC_RTU, SRC_TCP } DataSource;

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
extern bool pumpWereOn;
extern bool pumpWasOn;

// Previous values
extern bool     prevCoilLed_RTU;
extern bool     prevCoilPump_RTU;
extern bool     prevCoilLed2_RTU;
extern uint16_t prevSetTemp_RTU;
extern uint16_t prevSetSpeed_RTU;

extern bool     prevCoilLed_TCP;
extern bool     prevCoilPump_TCP;
extern bool     prevCoilLed2_TCP;
extern uint16_t prevSetTemp_TCP;
extern uint16_t prevSetSpeed_TCP;

// Timing
extern const unsigned long LOOP_INTERVAL_MS;
extern const unsigned long DHCP_RENEW_MS;
extern const unsigned long BUTTON_DEBOUNCE_MS;
extern unsigned long lastLoopTime;
extern unsigned long lastDHCPCheck;
extern unsigned long lastButtonChange;
extern bool apModeActive;
extern bool serverRoutesSetup;
extern bool lastButtonState;

// Utilities
void applyHardware();
void updateSimulatedMetrics();
