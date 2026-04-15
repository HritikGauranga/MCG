#include "Modem.h"
#include "Shared.h"
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <freertos/queue.h>

// Keep the modem on a dedicated UART so RTU on Serial2 can run concurrently.
HardwareSerial SerialAT(1);
bool modemReady = false;
static QueueHandle_t alertQueue = nullptr;

static bool enqueueAlert(AlertType type, uint32_t value) {
  if (alertQueue == nullptr) {
    return false;
  }

  AlertEvent event = {type, value};
  if (xQueueSend(alertQueue, &event, 0) != pdTRUE) {
    Serial.println("[ALERT] Queue full, dropping event");
    return false;
  }

  return true;
}

static String readSerialATResponse(unsigned long timeout) {
  String response = "";
  unsigned long start = millis();
  unsigned long lastByteAt = start;

  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      response += (char)SerialAT.read();
      lastByteAt = millis();
      delay(2);
    }

    // Once bytes have started arriving, give the modem a short quiet window
    // so we capture the full trailing OK / ERROR / +CMGS response.
    if (response.length() > 0 && (millis() - lastByteAt) > 250) {
      break;
    }

    delay(10);
  }

  response.trim();
  return response;
}

void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  Serial.println("[MODEM] Power ON triggered");
  delay(8000);
}

String sendAT(String cmd, int timeout) {
  while (SerialAT.available()) {
    SerialAT.read();
  }

  Serial.println("[AT] >> " + cmd);
  SerialAT.println(cmd);
  delay(100);

  String response = readSerialATResponse((unsigned long)timeout);

  if (response.length() == 0) {
    Serial.println("[AT] << [NO RESPONSE]");
  } else {
    Serial.println("[AT] << " + response);
  }

  return response;
}

bool waitForNetwork() {
  for (int i = 0; i < 10; i++) {
    String res = sendAT("AT+CREG?");
    if (res.indexOf("0,1") != -1 || res.indexOf("0,5") != -1) {
      Serial.println("[MODEM] Network Registered");
      return true;
    }

    Serial.println("[MODEM] Waiting for network...");
    delay(2000);
  }

  Serial.println("[MODEM] Network FAILED");
  return false;
}

void sendSMS(String number, String message) {
  if (!modemReady) {
    Serial.println("[SMS] ERROR: Modem not ready!");
    return;
  }

  Serial.printf("[SMS] Sending message to %s: %s\n", number.c_str(), message.c_str());

  sendAT("AT");
  sendAT("AT+CMEE=2", 2000);
  sendAT("AT+CPIN?", 2000);
  String signal = sendAT("AT+CSQ", 2000);
  Serial.printf("[SMS] Signal: %s\n", signal.c_str());

  if (!waitForNetwork()) {
    Serial.println("[SMS] Network not available!");
    return;
  }

  sendAT("AT+CSCS=\"GSM\"", 2000);
  sendAT("AT+CSMP=17,167,0,0", 2000);
  sendAT("AT+CMGF=1", 2000);

  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  delay(1000);
  SerialAT.print(message);
  delay(500);
  SerialAT.write(26);
  Serial.println("[SMS] CTRL+Z sent");

  Serial.println("[SMS] Waiting for modem send result...");
  String res = readSerialATResponse(15000);

  Serial.printf("[SMS] Modem response: %s\n", res.c_str());

  if (res.indexOf("+CMGS:") != -1 && res.indexOf("ERROR") == -1) {
    Serial.println("[SMS] Sent successfully");
  } else if (res.length() == 0) {
    Serial.println("[SMS] Send status unknown - no final modem response");
  } else {
    Serial.println("[SMS] Send status uncertain - modem did not return a clean +CMGS confirmation");
  }
}

void initModem() {
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
  Serial.println("[MODEM] Powering ON...");
  modemPowerOn();

  Serial.println("[MODEM] Testing AT connection...");
  String res = sendAT("AT", 2000);
  if (res.indexOf("OK") == -1) {
    Serial.println("[MODEM] Power ON: OK but no AT response yet, waiting...");
    delay(3000);
    res = sendAT("AT", 2000);
  }

  if (res.indexOf("OK") != -1) {
    Serial.println("[MODEM] AT connection OK");
    sendAT("AT+CMEE=2", 2000);
    sendAT("AT+CPIN?", 2000);
    sendAT("AT+CSQ", 2000);

    if (waitForNetwork()) {
      Serial.println("[MODEM] Modem initialized successfully");
      modemReady = true;
    } else {
      Serial.println("[MODEM] Network registration failed");
      modemReady = false;
    }
  } else {
    Serial.println("[MODEM] No modem response after power ON");
    modemReady = false;
  }
}

String getAlertMessage(String alertType) {
  if (!Shared_lockFileSystem()) {
    return "Alert: " + alertType;
  }

  File f = LittleFS.open("/alert_messages.csv", "r");
  if (!f) {
    Shared_unlockFileSystem();
    return "Alert: " + alertType;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.indexOf(alertType) != -1) {
      int commaIdx = line.indexOf(',');
      if (commaIdx != -1) {
        String message = line.substring(commaIdx + 1);
        f.close();
        Shared_unlockFileSystem();
        return message;
      }
    }
  }

  f.close();
  Shared_unlockFileSystem();
  return "Alert: " + alertType;
}

static bool sendAlertToEnabledNumbers(const String& alertKey, const String& placeholder, uint32_t value) {
  String msgTemplate = getAlertMessage(alertKey);

  if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) {
    Serial.println("[ALERT] File system busy, alert skipped");
    return false;
  }

  File f = LittleFS.open("/phone_numbers.csv", "r");
  if (!f) {
    Shared_unlockFileSystem();
    Serial.println("[ALERT] Unable to open phone number list");
    return false;
  }

  f.readStringUntil('\n');

  bool alerted = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      continue;
    }

    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    if (comma1 == -1 || comma2 == -1) {
      continue;
    }

    String number = line.substring(0, comma1);
    int enabled = line.substring(comma1 + 1, comma2).toInt();
    if (!enabled) {
      continue;
    }

    String msg = msgTemplate;
    if (placeholder.length() > 0) {
      msg.replace(placeholder, String(value));
    }

    Serial.printf("[ALERT] Sending to: %s\n", number.c_str());
    sendSMS(number, msg);
    alerted = true;
  }

  f.close();
  Shared_unlockFileSystem();

  if (!alerted) {
    Serial.println("[ALERT] WARNING: No enabled phone numbers found");
  }

  return alerted;
}

bool Alerts_init(size_t queueLength) {
  if (alertQueue == nullptr) {
    alertQueue = xQueueCreate(queueLength, sizeof(AlertEvent));
  }

  return alertQueue != nullptr;
}

void checkAndQueueAlerts() {
  static bool pumpStateSeen = false;
  static unsigned long localPumpOnStart = 0;
  static unsigned long localLastTempAlert = 0;
  static unsigned long localLastSpeedAlert = 0;
  static unsigned long localLastPumpAlert = 0;
  static unsigned long localLastPumpOnAlert = 0;
  static unsigned long localLastPumpOffAlert = 0;

  SystemState snapshot = Shared_getSnapshot();
  unsigned long now = millis();

  if (snapshot.actualTemp > TEMP_ALERT_THRESHOLD) {
    if (now - localLastTempAlert > ALERT_COOLDOWN && enqueueAlert(ALERT_TEMP_HIGH, snapshot.actualTemp)) {
      localLastTempAlert = now;
      lastTempAlert = now;
      Serial.printf("[ALERT] Temperature event queued: %u\n", snapshot.actualTemp);
    }
  }

  if (snapshot.setSpeed > SPEED_ALERT_THRESHOLD) {
    if ((localLastSpeedAlert == 0 || now - localLastSpeedAlert > ALERT_COOLDOWN) &&
        enqueueAlert(ALERT_SPEED_HIGH, snapshot.setSpeed)) {
      localLastSpeedAlert = now;
      lastSpeedAlert = now;
      Serial.printf("[ALERT] Speed event queued: %u\n", snapshot.setSpeed);
    }
  }

  if (snapshot.coilPump && !pumpStateSeen) {
    localPumpOnStart = now;
    pumpStateSeen = true;
    pumpOnStart = now;
    pumpWasOn = true;

    if ((localLastPumpOnAlert == 0 || now - localLastPumpOnAlert > ALERT_COOLDOWN) &&
        enqueueAlert(ALERT_PUMP_ON, 0)) {
      localLastPumpOnAlert = now;
      lastPumpOnAlert = now;
      Serial.println("[ALERT] Pump ON event queued");
    }
  } else if (!snapshot.coilPump && pumpStateSeen) {
    unsigned long duration = (now - localPumpOnStart) / 1000;
    pumpStateSeen = false;
    pumpWasOn = false;

    if ((localLastPumpOffAlert == 0 || now - localLastPumpOffAlert > ALERT_COOLDOWN) &&
        enqueueAlert(ALERT_PUMP_OFF, duration)) {
      localLastPumpOffAlert = now;
      lastPumpOffAlert = now;
      Serial.printf("[ALERT] Pump OFF event queued: %lu sec\n", duration);
    }

    if (duration > (PUMP_TIME_ALERT / 1000) &&
        (localLastPumpAlert == 0 || now - localLastPumpAlert > ALERT_COOLDOWN) &&
        enqueueAlert(ALERT_PUMP_DURATION, duration)) {
      localLastPumpAlert = now;
      lastPumpAlert = now;
      Serial.printf("[ALERT] Pump duration event queued: %lu sec\n", duration);
    }
  }
}

void Alerts_task(void *pvParameters) {
  (void)pvParameters;
  AlertEvent event;

  for (;;) {
    if (alertQueue == nullptr || xQueueReceive(alertQueue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (!modemReady) {
      Serial.println("[ALERT] Initializing modem on demand...");
      initModem();
    }

    if (!modemReady) {
      Serial.println("[ALERT] Modem unavailable, event dropped");
      continue;
    }

    switch (event.type) {
      case ALERT_TEMP_HIGH:
        Serial.printf("[ALERT] Dispatching temperature alert: %lu\n", event.value);
        sendAlertToEnabledNumbers("TEMP_HIGH", "{TEMP}", event.value);
        break;

      case ALERT_SPEED_HIGH:
        Serial.printf("[ALERT] Dispatching speed alert: %lu\n", event.value);
        sendAlertToEnabledNumbers("SPEED_HIGH", "{SPEED}", event.value);
        break;

      case ALERT_PUMP_ON:
        Serial.println("[ALERT] Dispatching pump ON alert");
        sendAlertToEnabledNumbers("PUMP_ON", "", event.value);
        break;

      case ALERT_PUMP_OFF:
        Serial.printf("[ALERT] Dispatching pump OFF alert: %lu sec\n", event.value);
        sendAlertToEnabledNumbers("PUMP_OFF", "{DURATION}", event.value);
        break;

      case ALERT_PUMP_DURATION:
        Serial.printf("[ALERT] Dispatching pump duration alert: %lu sec\n", event.value);
        sendAlertToEnabledNumbers("PUMP_DURATION", "{DURATION}", event.value);
        break;
    }
  }
}
