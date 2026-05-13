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

  // -------------------------------------------------------------------------
  // Stack monitor template (kept commented for future testing)
  //
  // 1) Uncomment the TaskHandle_t variables below.
  // 2) Pass these handles in xTaskCreatePinnedToCore(..., &handle, ...).
  // 3) Uncomment printStackUsage() and call it periodically from loop().
  //
  // NOTE: Stack values are in FreeRTOS "words" (not bytes).
  // -------------------------------------------------------------------------
  /*
  TaskHandle_t gSmsTaskHandle = nullptr;
  TaskHandle_t gRtuTaskHandle = nullptr;
  TaskHandle_t gTcpTaskHandle = nullptr;
  TaskHandle_t gApTaskHandle  = nullptr;

  void printStackUsage(const char *name, TaskHandle_t handle, uint32_t configuredWords) {
    if (handle == nullptr || configuredWords == 0) return;

    UBaseType_t minFreeWords = uxTaskGetStackHighWaterMark(handle); // minimum ever free
    uint32_t usedWords = configuredWords - (uint32_t)minFreeWords;
    float headroomPct = (100.0f * (float)minFreeWords) / (float)configuredWords;

    Serial.printf("[STACK] %s used=%lu free=%lu headroom=%.1f%%\n",
                  name,
                  (unsigned long)usedWords,
                  (unsigned long)minFreeWords,
                  headroomPct);
  }
  */
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== MB Map RTOS SMS Controller ===");

  Shared_init();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(AP_STATUS_LED_PIN, OUTPUT);
  digitalWrite(AP_STATUS_LED_PIN, LOW);

  if (!LittleFS.begin(true)) {
    Serial.println("[ERROR] LittleFS mount failed — halting");
    while (true) delay(1000);
  }

  ensureMBMapConfigFile();
  Shared_loadMessageConfig();
  Shared_loadGatewaySettings();
  printMBMapSummary();
  printAPStatus();

  // Keep lwIP active for always-on Web UI; AP task switches to AP_STA when needed.
  WiFi.mode(WIFI_STA);
  delay(100);

  RTU_init();
  TCP_init();

  if (!Modem_init()) {
    Serial.println("[MODEM] Modem init setup failed — halting");
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
  // Stack monitor ready-to-enable versions:
  // xTaskCreatePinnedToCore(Modem_task,   "SmsTask",  MODEM_TASK_STACK, nullptr, 2, &gSmsTaskHandle, 0);
  // xTaskCreatePinnedToCore(RTU_taskLoop, "RTUTask",  RTU_TASK_STACK,   nullptr, 3, &gRtuTaskHandle, 1);
  // xTaskCreatePinnedToCore(TCP_taskLoop, "TCPTask",  TCP_TASK_STACK,   nullptr, 2, &gTcpTaskHandle, 1);
  // xTaskCreatePinnedToCore(AP_taskLoop,  "ApTask",   AP_TASK_STACK,    nullptr, 1, &gApTaskHandle, 1);

  Serial.println("[SYSTEM] Tasks started: SmsTask, RTUTask, TCPTask, ApTask");
}


void loop() {
  // Uncomment for periodic stack report during stress tests:
  // printStackUsage("SmsTask", gSmsTaskHandle, MODEM_TASK_STACK);
  // printStackUsage("RTUTask", gRtuTaskHandle, RTU_TASK_STACK);
  // printStackUsage("TCPTask", gTcpTaskHandle, TCP_TASK_STACK);
  // printStackUsage("ApTask",  gApTaskHandle,  AP_TASK_STACK);
  // vTaskDelay(pdMS_TO_TICKS(5000));

  vTaskDelay(pdMS_TO_TICKS(1000));
}
