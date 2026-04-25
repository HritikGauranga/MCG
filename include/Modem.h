#pragma once
#include <Arduino.h>

extern bool modemReady;

struct SmsJob {
  uint8_t messageIndex;
};

void initModem();
String sendAT(const String &cmd, int timeout = 3000);
bool waitForNetwork();
bool modemSimReady();
bool sendSMS(const String &number, const String &message);
bool Modem_init(size_t queueLength = 16);
void Modem_task(void *pvParameters);