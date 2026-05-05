#pragma once
#include <Arduino.h>

void ensureMBMapConfigFile();
void printMBMapSummary();
void printAPStatus();
void AP_taskLoop(void *pvParameters);
