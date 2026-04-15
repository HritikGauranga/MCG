#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

extern bool modemReady;

enum AlertType {
  ALERT_TEMP_HIGH,
  ALERT_SPEED_HIGH,
  ALERT_PUMP_ON,
  ALERT_PUMP_OFF,
  ALERT_PUMP_DURATION
};

struct AlertEvent {
  AlertType type;
  uint32_t value;
};

void initModem();
String sendAT(String cmd, int timeout = 3000);
bool waitForNetwork();
void sendSMS(String number, String message);
String getAlertMessage(String alertType);
bool Alerts_init(size_t queueLength = 10);
void Alerts_task(void *pvParameters);
void checkAndQueueAlerts();
