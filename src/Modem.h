#pragma once
#include <Arduino.h>

extern bool modemReady;

void initModem();
String sendAT(String cmd, int timeout = 3000);
bool waitForNetwork();
void sendSMS(String number, String message);
String getAlertMessage(String alertType);
void checkAndSendAlerts();
void modemKeepAlive();  // Keep modem connection alive
