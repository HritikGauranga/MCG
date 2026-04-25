#pragma once
#include <Arduino.h>

String htmlPage();
void setupWebServerRoutes();
void startAPMode();
void stopAPMode();
void ensureMBMapConfigFile();
void printMBMapSummary();
void printAPStatus();
void AP_taskLoop(void *pvParameters);