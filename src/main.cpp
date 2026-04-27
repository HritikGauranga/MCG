#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "AP.h"
#include "Modem.h"
#include "RTU.h"
#include "Shared.h"
#include "TCP.h"

namespace {
  constexpr uint32_t RTU_TASK_STACK   = 4096;
  constexpr uint32_t TCP_TASK_STACK   = 6144;
  constexpr uint32_t MODEM_TASK_STACK = 8192;
  constexpr uint32_t AP_TASK_STACK    = 4096;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== MB Map RTOS SMS Controller ===");

  Shared_init();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true)) {
    Serial.println("[ERROR] LittleFS mount failed — halting");
    while (true) delay(1000);
  }

  ensureMBMapConfigFile();
  Shared_loadMessageConfig();
  printMBMapSummary();
  printAPStatus();

  WiFi.mode(WIFI_OFF);
  delay(100);

  RTU_init();
  TCP_init();

  if (!Modem_init()) {
    Serial.println("[MODEM] Failed to create SMS queue — halting");
    while (true) delay(1000);
  }

  // ---------------------------------------------------------------------------
  // Task layout — 4 tasks total:
  //
  //   Core 0: SmsTask  (priority 2)
  //     Calls initModem() at startup (~15s), then scans edges and sends SMS.
  //     Core 1 runs freely during modem init — no blocking effect on Modbus.
  //
  //   Core 1: RTUTask  (priority 3) — Modbus RTU, highest prio on core 1
  //           TCPTask  (priority 2) — Modbus TCP
  //           ApTask   (priority 1) — Wi-Fi AP config server, lowest prio
  // ---------------------------------------------------------------------------

  xTaskCreatePinnedToCore(Modem_task,   "SmsTask",  MODEM_TASK_STACK, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(RTU_taskLoop, "RTUTask",  RTU_TASK_STACK,   nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(TCP_taskLoop, "TCPTask",  TCP_TASK_STACK,   nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(AP_taskLoop,  "ApTask",   AP_TASK_STACK,    nullptr, 1, nullptr, 1);

  Serial.println("[SYSTEM] Tasks started: SmsTask, RTUTask, TCPTask, ApTask");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}