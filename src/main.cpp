// // =============================================================================
// // main.cpp  —  for use with Modem_A.cpp (boot-time modem init)
// // =============================================================================

// #include <Arduino.h>
// #include <LittleFS.h>
// #include <WiFi.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>

// #include "AP.h"
// #include "Modem.h"
// #include "RTU.h"
// #include "Shared.h"
// #include "TCP.h"

// namespace {
//   constexpr uint32_t RTU_TASK_STACK   = 4096;
//   constexpr uint32_t TCP_TASK_STACK   = 6144;
//   constexpr uint32_t MODEM_TASK_STACK = 8192;
//   constexpr uint32_t AP_TASK_STACK    = 4096;
// }

// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   Serial.println("\n=== MB Map RTOS SMS Controller (Option A — boot-time init) ===");

//   Shared_init();
//   pinMode(BUTTON_PIN, INPUT_PULLUP);

//   if (!LittleFS.begin(true)) {
//     Serial.println("[ERROR] LittleFS mount failed — halting");
//     while (true) delay(1000);
//   }

//   ensureMBMapConfigFile();
//   Shared_loadMessageConfig();
//   printMBMapSummary();
//   printAPStatus();

//   WiFi.mode(WIFI_OFF);
//   delay(100);

//   // Peripheral init
//   RTU_init();
//   TCP_init();

//   // Modem init happens here at boot — takes ~10-15 s
//   if (!Modem_init()) {
//     Serial.println("[MODEM] Failed to create SMS queue — halting");
//     while (true) delay(1000);
//   }
//   initModem();  // Blocks until modem is up (or fails)

//   // ---------------------------------------------------------------------------
//   // Task layout:
//   //   Core 0: SmsTask  (priority 2) — SMS processing, modem comms
//   //   Core 1: RTUTask  (priority 3) — Modbus RTU (higher prio than TCP so
//   //                                   serial frames are never dropped)
//   //           TCPTask  (priority 2) — Modbus TCP
//   //           ApTask   (priority 1) — Wi-Fi AP config server (lowest)
//   // ---------------------------------------------------------------------------

//   xTaskCreatePinnedToCore(RTU_taskLoop,  "RTUTask",  RTU_TASK_STACK,   nullptr, 3, nullptr, 1);
//   xTaskCreatePinnedToCore(TCP_taskLoop,  "TCPTask",  TCP_TASK_STACK,   nullptr, 2, nullptr, 1);
//   xTaskCreatePinnedToCore(Modem_task,    "SmsTask",  MODEM_TASK_STACK, nullptr, 2, nullptr, 0);
//   xTaskCreatePinnedToCore(AP_taskLoop,   "ApTask",   AP_TASK_STACK,    nullptr, 1, nullptr, 1);

//   Serial.println("[SYSTEM] Tasks started: RTUTask, TCPTask, SmsTask, ApTask");
// }

// void loop() {
//   vTaskDelay(pdMS_TO_TICKS(1000));
// }

// =============================================================================
// main.cpp  —  for use with Modem_B.cpp (background modem init)
// =============================================================================

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
  constexpr uint32_t RTU_TASK_STACK        = 4096;
  constexpr uint32_t TCP_TASK_STACK        = 6144;
  constexpr uint32_t MODEM_TASK_STACK      = 8192;
  constexpr uint32_t MODEM_INIT_TASK_STACK = 4096;
  constexpr uint32_t AP_TASK_STACK         = 4096;
}

// Declared in Modem_B.cpp
extern void Modem_initTask(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== MB Map RTOS SMS Controller (Option B — background init) ===");

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
  // Task layout:
  //   Core 0: ModemInit (priority 4, one-shot — deletes itself after init)
  //           SmsTask   (priority 2 — blocks on semaphore until init done)
  //   Core 1: RTUTask   (priority 3)
  //           TCPTask   (priority 2)
  //           ApTask    (priority 1)
  //
  // ModemInit runs at priority 4 so it isn't preempted during the critical
  // AT command sequence. SmsTask starts immediately but blocks on the binary
  // semaphore released by ModemInit, so no SMS jobs are processed until
  // the modem is ready.
  // ---------------------------------------------------------------------------

  // Create ModemInit BEFORE SmsTask so the semaphore is given before
  // SmsTask could theoretically time out (it uses portMAX_DELAY so it won't,
  // but ordering is cleaner).
  xTaskCreatePinnedToCore(Modem_initTask, "ModemInit", MODEM_INIT_TASK_STACK, nullptr, 4, nullptr, 0);
  xTaskCreatePinnedToCore(Modem_task,     "SmsTask",   MODEM_TASK_STACK,      nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(RTU_taskLoop,   "RTUTask",   RTU_TASK_STACK,        nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(TCP_taskLoop,   "TCPTask",   TCP_TASK_STACK,        nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(AP_taskLoop,    "ApTask",    AP_TASK_STACK,         nullptr, 1, nullptr, 1);

  Serial.println("[SYSTEM] Tasks started: ModemInit, SmsTask, RTUTask, TCPTask, ApTask");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}