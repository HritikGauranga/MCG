#include "Modem.h"
#include "Shared.h"
#include <HardwareSerial.h>
#include <LittleFS.h>

HardwareSerial SerialAT(2);
bool modemReady = false;

void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  Serial.println("[MODEM] Power ON triggered");
  delay(8000);  // wait for boot
}

void modemPowerOff() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  Serial.println("[MODEM] Power OFF triggered");
  delay(3000);  // wait for shutdown
}

String sendAT(String cmd, int timeout) {
  String response = "";
  Serial.println("[AT] >> " + cmd);
  SerialAT.println(cmd);
  
  long start = millis();
  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }
  }
  
  if (response.length() > 0) {
    Serial.println("[AT] << " + response.substring(0, 100));
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
    Serial.println("[SMS] Modem not ready!");
    return;
  }

  sendAT("AT");
  sendAT("AT+CMEE=2");
  sendAT("AT+CPIN?");
  sendAT("AT+CSQ");

  if (!waitForNetwork()) return;

  sendAT("AT+CSCS=\"GSM\"");
  sendAT("AT+CSMP=17,167,0,0");
  sendAT("AT+CMGF=1");

  Serial.println("[SMS] Sending to " + number + ": " + message);

  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  delay(1000);
  SerialAT.print(message);
  delay(500);
  SerialAT.write(26);  // CTRL+Z

  Serial.println("[SMS] Waiting for response...");
  delay(5000);

  String res = "";
  while (SerialAT.available()) {
    res += (char)SerialAT.read();
  }

  if (res.indexOf("+CMGS:") != -1) {
    Serial.println("[SMS] Sent successfully!");
  } else {
    Serial.println("[SMS] Failed");
  }
}

void initModem() {
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(100);

  Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
  Serial.println("[MODEM] Performing power cycle (OFF→ON→OFF→ON)...");

  // POWER CYCLE: OFF → ON → OFF → ON
  Serial.println("[MODEM] Step 1: Power ON");
  modemPowerOn();

  Serial.println("[MODEM] Step 2: Power OFF");
  modemPowerOff();

  Serial.println("[MODEM] Step 3: Power ON (Final)");
  modemPowerOn();

  delay(2000);
  Serial.println("[MODEM] Power cycle complete. Testing connection...");

  String res = sendAT("AT");
  if (res.indexOf("OK") != -1) {
    Serial.println("[MODEM] Modem found!");
    modemReady = true;
  } else {
    Serial.println("[MODEM] Modem not responding");
    modemReady = false;
  }
}

String getAlertMessage(String alertType) {
  File f = LittleFS.open("/alert_messages.csv", "r");
  if (!f) return "Alert: " + alertType;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.indexOf(alertType) != -1) {
      int commaIdx = line.indexOf(',');
      if (commaIdx != -1) {
        f.close();
        return line.substring(commaIdx + 1);
      }
    }
  }
  f.close();
  return "Alert: " + alertType;
}

void checkAndSendAlerts() {
  if (!modemReady) return;

  unsigned long now = millis();

  // Temperature Alert
  if (actualTemp > TEMP_ALERT_THRESHOLD) {
    if (now - lastTempAlert > ALERT_COOLDOWN) {
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          String msg = getAlertMessage("TEMP_HIGH");
          msg.replace("{TEMP}", String(actualTemp));
          sendSMS(number, msg);
        }
      }
      f.close();
      lastTempAlert = now;
      Serial.println("[ALERT] Temperature alert sent");
    }
  }

  // Speed Alert
  if (setSpeed > SPEED_ALERT_THRESHOLD) {
    if (now - lastSpeedAlert > ALERT_COOLDOWN) {
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          String msg = getAlertMessage("SPEED_HIGH");
          msg.replace("{SPEED}", String(setSpeed));
          sendSMS(number, msg);
        }
      }
      f.close();
      lastSpeedAlert = now;
      Serial.println("[ALERT] Speed alert sent");
    }
  }

  // Pump On Duration Alert
  if (coilPump && !pumpWasOn) {
    pumpOnStart = now;
    pumpWasOn = true;
  } else if (!coilPump && pumpWasOn) {
    unsigned long duration = (now - pumpOnStart) / 1000;
    pumpWasOn = false;

    if (duration > (PUMP_TIME_ALERT / 1000) && now - lastPumpAlert > ALERT_COOLDOWN) {
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          String msg = getAlertMessage("PUMP_DURATION");
          msg.replace("{DURATION}", String(duration));
          sendSMS(number, msg);
        }
      }
      f.close();
      lastPumpAlert = now;
      Serial.println("[ALERT] Pump duration alert sent");
    }
  }
}
