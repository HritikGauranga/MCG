// Refactored main orchestrator - uses module files for RTU, TCP, AP, Modem and shared state
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "Shared.h"
#include "AP.h"
#include "RTU.h"
#include "TCP.h"
#include "Modem.h"

void checkButtonPress() {
  bool buttonState = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  // Detect state CHANGE with debouncing (latching switch has two stable states)
  if (buttonState != lastButtonState && (now - lastButtonChange > BUTTON_DEBOUNCE_MS)) {
    lastButtonState = buttonState;
    lastButtonChange = now;

    // Latching switch: LOW = ON, HIGH = OFF (typical)
    if (buttonState == LOW) {
      // Switch turned ON
      if (!apModeActive) startAPMode();
    } else {
      // Switch turned OFF
      if (apModeActive) stopAPMode();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== WebMOD Async File Manager (Latching Switch AP Control) ===");

  // Configure latching switch pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println(String("Latching switch on GPIO ") + String(BUTTON_PIN) + " ready (ON position = enable WebUI, OFF position = disable)");

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed!");
    return;
  }

  Serial.println("LittleFS initialized. Web routes will be set up when AP mode is activated.");

  // Create sample CSV files (AP module)
  createSampleCSVFiles();

  // Print loaded phone numbers to serial monitor
  printPhoneNumbers();

  // Disable WiFi to avoid conflicts with Ethernet
  WiFi.mode(WIFI_OFF);
  delay(100);
  Serial.println("WiFi disabled (will enable when AP button is activated)");

  // RTU init
  delay(500);
  Serial.println("\n=== Initializing RTU (Modbus) ===");
  RTU_init();

  // Ethernet + TCP init
  Serial.println("\n=== Initializing Ethernet (DHCP) ===");
  TCP_init();

  Serial.println("\n=== System Initialize Complete ===");
  Serial.println("Modem will be initialized on-demand when first alert is triggered\n");
}

void loop() {
  // RTU task must run every iteration
  RTU_task();

  unsigned long now = millis();
  if (now - lastLoopTime < LOOP_INTERVAL_MS) return;
  lastLoopTime = now;

  // Check button press for AP mode toggle
  checkButtonPress();

  // Network & DHCP tasks — always run so TCP/Modbus is serviced
  TCP_maintainDHCP();
  TCP_processNetwork();

  // Sync data between TCP, RTU, and hardware
  RTU_syncFrom();
  TCP_syncFrom();

  // Apply hardware changes and metrics
  applyHardware();
  updateSimulatedMetrics();

  // Check for alert conditions and send SMS
  checkAndSendAlerts();

  // Sync back to both TCP and RTU
  RTU_syncTo();
  TCP_syncTo();
}
