#pragma once
#include <Arduino.h>

extern bool modemReady;

void initModem();
String sendAT(const String &cmd, int timeout = 3000);
bool waitForNetwork();
bool modemSimReady();
bool sendSMS(const String &number, const String &message);
bool Modem_init();
void Modem_task(void *pvParameters);
