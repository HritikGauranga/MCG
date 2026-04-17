// RTOS-based main orchestrator - splits comms, control, and alert delivery into tasks
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Shared.h"
#include "AP.h"
#include "RTU.h"
#include "TCP.h"
#include "Modem.h"

namespace {
constexpr uint32_t COMM_TASK_DELAY_MS = 5;
constexpr uint32_t CONTROL_TASK_DELAY_MS = 50;
constexpr uint32_t ALERT_TASK_STACK = 8192;
constexpr uint32_t COMM_TASK_STACK = 6144;
constexpr uint32_t CONTROL_TASK_STACK = 4096;
}

static void handleAPSwitch() {
  static bool lastButtonState = HIGH;
  static unsigned long lastButtonChange = 0;

  bool buttonState = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (buttonState != lastButtonState && (now - lastButtonChange > BUTTON_DEBOUNCE_MS)) {
    lastButtonState = buttonState;
    lastButtonChange = now;

    if (buttonState == LOW) {
      startAPMode();
    } else {
      stopAPMode();
    }
  }
}

static void communicationTask(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    RTU_task();
    TCP_processNetwork();
    RTU_syncFrom();
    TCP_syncFrom();
    RTU_syncTo();
    TCP_syncTo();
    TCP_maintainDHCP();
    vTaskDelay(pdMS_TO_TICKS(COMM_TASK_DELAY_MS));
  }
}

static void controlTask(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    handleAPSwitch();
    updateSimulatedMetrics();
    applyHardware();
    checkAndQueueAlerts();
    vTaskDelay(pdMS_TO_TICKS(CONTROL_TASK_DELAY_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== WebMOD RTOS Control System ===");
  Shared_init();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println(String("Latching switch on GPIO ") + String(BUTTON_PIN) + " ready (ON position = enable WebUI, OFF position = disable)");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed!");
    return;
  }

  createSampleCSVFiles();
  printPhoneNumbers();

  WiFi.mode(WIFI_OFF);
  delay(100);
  Serial.println("WiFi disabled (will enable when AP button is activated)");

  Serial.println("\n=== Initializing RTU (Modbus) ===");
  RTU_init();

  Serial.println("\n=== Initializing Ethernet (DHCP) ===");
  TCP_init();

  if (!Alerts_init()) {
    Serial.println("[ALERT] Failed to create alert queue");
  }

  xTaskCreatePinnedToCore(
    communicationTask, // Task function
    "CommTask", // Task name
    COMM_TASK_STACK, // Stack size: this is define in 
    nullptr, // Task parameters (not used) //example if used: could pass a pointer to a shared data structure if needed
    3, // Priority: higher than control and alert tasks to ensure responsive comms
    nullptr, // Task handle (not used): example if used: could store the handle to manage the task later (e.g., suspend/resume)
    1 // Core affinity: pin to core 1 to keep it separate from control and alert tasks on core 0
  );
  xTaskCreatePinnedToCore(
    controlTask, 
    "ControlTask", 
    CONTROL_TASK_STACK, 
    nullptr, 
    2, 
    nullptr, 
    1
  );
  xTaskCreatePinnedToCore(
    Alerts_task, 
    "AlertTask", 
    ALERT_TASK_STACK, 
    nullptr, 
    1, 
    nullptr, 
    0
  );

  Serial.println("\n=== RTOS Startup Complete ===");
  Serial.println("Tasks: CommTask, ControlTask, AlertTask");
  Serial.println("Modem will be initialized on-demand when the first alert is dispatched\n");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

