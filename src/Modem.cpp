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

String sendAT(String cmd, int timeout) {
  // Clear input buffer first
  while (SerialAT.available()) {
    SerialAT.read();
  }
  
  String response = "";
  Serial.println("[AT] >> " + cmd);
  SerialAT.println(cmd);
  delay(100);  // Give modem time to start responding
  
  long start = millis();
  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      delay(5);  // Let more bytes arrive
    }
  }
  
  // Trim whitespace
  response.trim();
  
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

  Serial.printf("[SMS] Sending message to %s: %s\n", number.c_str(), message.c_str());

  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  delay(1000);
  SerialAT.print(message);
  delay(500);
  SerialAT.write(26);  // CTRL+Z
  Serial.println("[SMS] CTRL+Z sent");

  Serial.println("[SMS] Waiting for +CMGS response...");
  delay(5000);

  String res = "";
  while (SerialAT.available()) {
    res += (char)SerialAT.read();
    delay(5);
  }

  Serial.printf("[SMS] Modem response: %s\n", res.c_str());

  if (res.indexOf("+CMGS:") != -1) {
    Serial.println("[SMS] ✓ Sent successfully!");
  } else {
    Serial.println("[SMS] ✗ Send failed - no +CMGS response");
  }
}

void initModem() {
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
  
  // Power ON
  Serial.println("[MODEM] Powering ON...");
  modemPowerOn();
  
  // Test AT connection
  Serial.println("[MODEM] Testing AT connection...");
  String res = sendAT("AT", 2000);
  if (res.indexOf("OK") == -1) {
    Serial.println("[MODEM] Power ON: OK but no AT response yet, waiting...");
    delay(3000);
    res = sendAT("AT", 2000);
  }
  
  if (res.indexOf("OK") != -1) {
    Serial.println("[MODEM] ✓ AT connection OK");
    
    // Configure modem
    sendAT("AT+CMEE=2", 2000);      // Error reporting enabled
    sendAT("AT+CPIN?", 2000);       // Check SIM
    sendAT("AT+CSQ", 2000);         // Signal quality
    
    // Wait for network
    if (waitForNetwork()) {
      Serial.println("[MODEM] ✓ Modem initialized successfully!");
      modemReady = true;
    } else {
      Serial.println("[MODEM] Network registration failed");
      modemReady = false;
    }
  } else {
    Serial.println("[MODEM] ✗ No modem response after power ON");
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
  unsigned long now = millis();

  // Temperature Alert
  if (actualTemp > TEMP_ALERT_THRESHOLD) {
    Serial.printf("[ALERT] Temperature threshold exceeded! Current: %d°C (Limit: %d°C)\n", actualTemp, TEMP_ALERT_THRESHOLD);
    

    if (now - lastTempAlert > ALERT_COOLDOWN) {
      // Initialize modem only when we need to send SMS
      if (!modemReady) {
        Serial.println("[ALERT] Initializing modem for temperature alert...");
        initModem();
      }
      
      Serial.println("[ALERT] Sending temperature alerts...");
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      bool alerted = false;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          Serial.printf("[ALERT] Sending to: %s\n", number.c_str());
          String msg = getAlertMessage("TEMP_HIGH");
          msg.replace("{TEMP}", String(actualTemp));
          sendSMS(number, msg);
          alerted = true;
          Serial.printf("[ALERT] ✓ ACK: Temperature alert sent to %s\n", number.c_str());
        }
      }
      f.close();
      lastTempAlert = now;
      if (alerted) {
        Serial.println("[ALERT] ✓ All temperature alerts completed");
      } else {
        Serial.println("[ALERT] WARNING: No enabled phone numbers found");
      }
    } else {
      Serial.println("[ALERT] Temperature cooldown active - skipping");
    }
    return;
  }

  // Speed Alert
  if (setSpeed > SPEED_ALERT_THRESHOLD) {
    Serial.printf("[ALERT] Speed threshold exceeded! Current: %d RPM (Limit: %d RPM)\n", setSpeed, SPEED_ALERT_THRESHOLD);
    
    if (lastSpeedAlert == 0 || (now - lastSpeedAlert) > ALERT_COOLDOWN) {
      // Initialize modem only when we need to send SMS
      if (!modemReady) {
        Serial.println("[ALERT] Initializing modem for speed alert...");
        initModem();
      }
      
      Serial.println("[ALERT] Sending speed alerts...");
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      bool alerted = false;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          Serial.printf("[ALERT] Sending to: %s\n", number.c_str());
          String msg = getAlertMessage("SPEED_HIGH");
          msg.replace("{SPEED}", String(setSpeed));
          sendSMS(number, msg);
          alerted = true;
          Serial.printf("[ALERT] ✓ ACK: Speed alert sent to %s\n", number.c_str());
        }
      }
      f.close();
      lastSpeedAlert = now;
      if (alerted) {
        Serial.println("[ALERT] ✓ All speed alerts completed");
      } else {
        Serial.println("[ALERT] WARNING: No enabled phone numbers found");
      }
    } else {
      Serial.println("[ALERT] Speed cooldown active - skipping");
    }
    return;
  }

  // Pump State Tracking
  if (coilPump && !pumpWasOn) {
    // Pump just turned ON
    pumpOnStart = now;
    pumpWasOn = true;
    Serial.println("[ALERT] Pump turned ON - sending alert...");
    
    if (lastPumpOnAlert == 0 || (now - lastPumpOnAlert) > ALERT_COOLDOWN) {
      // Initialize modem only when we need to send SMS
      if (!modemReady) {
        Serial.println("[ALERT] Initializing modem for pump ON alert...");
        initModem();
      }
      
      Serial.println("[ALERT] Sending pump ON alerts...");
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      bool alerted = false;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          Serial.printf("[ALERT] Sending to: %s\n", number.c_str());
          String msg = getAlertMessage("PUMP_ON");
          sendSMS(number, msg);
          alerted = true;
          Serial.printf("[ALERT] ✓ ACK: Pump ON alert sent to %s\n", number.c_str());
        }
      }
      f.close();
      lastPumpOnAlert = now;
      if (alerted) {
        Serial.println("[ALERT] ✓ All pump ON alerts completed");
      } else {
        Serial.println("[ALERT] WARNING: No enabled phone numbers found");
      }
    } else {
      Serial.println("[ALERT] Pump ON cooldown active - skipping");
    }
  } else if (!coilPump && pumpWasOn) {
    // Pump just turned OFF
    unsigned long duration = (now - pumpOnStart) / 1000;
    pumpWasOn = false;
    Serial.printf("[ALERT] Pump turned OFF (duration: %lu sec) - sending alert...\n", duration);

    if (lastPumpOffAlert == 0 || (now - lastPumpOffAlert) > ALERT_COOLDOWN) {
      // Initialize modem only when we need to send SMS
      if (!modemReady) {
        Serial.println("[ALERT] Initializing modem for pump OFF alert...");
        initModem();
      }
      
      Serial.println("[ALERT] Sending pump OFF alerts...");
      File f = LittleFS.open("/phone_numbers.csv", "r");
      f.readStringUntil('\n');  // skip header

      bool alerted = false;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;

        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);

        String number = line.substring(0, comma1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();

        if (enabled) {
          Serial.printf("[ALERT] Sending to: %s\n", number.c_str());
          String msg = getAlertMessage("PUMP_OFF");
          msg.replace("{DURATION}", String(duration));
          sendSMS(number, msg);
          alerted = true;
          Serial.printf("[ALERT] ✓ ACK: Pump OFF alert sent to %s\n", number.c_str());
        }
      }
      f.close();
      lastPumpOffAlert = now;
      if (alerted) {
        Serial.println("[ALERT] ✓ All pump OFF alerts completed");
      } else {
        Serial.println("[ALERT] WARNING: No enabled phone numbers found");
      }
    } else {
      Serial.println("[ALERT] Pump OFF cooldown active - skipping");
    }

    // Check if pump ran too long
    if (duration > (PUMP_TIME_ALERT / 1000)) {
      Serial.printf("[ALERT] Pump duration threshold exceeded! Duration: %lu sec (Limit: %lu sec)\n", duration, PUMP_TIME_ALERT / 1000);
      
      if (now - lastPumpAlert > ALERT_COOLDOWN) {
        Serial.println("[ALERT] Sending pump duration alerts...");
        File f = LittleFS.open("/phone_numbers.csv", "r");
        f.readStringUntil('\n');  // skip header

        bool alerted = false;
        while (f.available()) {
          String line = f.readStringUntil('\n');
          if (line.length() == 0) continue;

          int comma1 = line.indexOf(',');
          int comma2 = line.indexOf(',', comma1 + 1);

          String number = line.substring(0, comma1);
          int enabled = line.substring(comma1 + 1, comma2).toInt();

          if (enabled) {
            Serial.printf("[ALERT] Sending to: %s\n", number.c_str());
            String msg = getAlertMessage("PUMP_DURATION");
            msg.replace("{DURATION}", String(duration));
            sendSMS(number, msg);
            alerted = true;
            Serial.printf("[ALERT] ✓ ACK: Pump duration alert sent to %s\n", number.c_str());
          }
        }
        f.close();
        lastPumpAlert = now;
        if (alerted) {
          Serial.println("[ALERT] ✓ All pump duration alerts completed");
        } else {
          Serial.println("[ALERT] WARNING: No enabled phone numbers found");
        }
      }
    }
  }
}

// Keepalive - ping modem every 5 seconds to prevent timeout
void modemKeepAlive() {
  unsigned long now = millis();
  
  // Ping every 5 seconds if modem is ready
  if (modemReady && (now - lastModemKeepAlive) > MODEM_KEEPALIVE_INTERVAL) {
    String res = sendAT("AT", 1000);
    if (res.indexOf("OK") != -1) {
      // Connection alive, update timestamp
      lastModemKeepAlive = now;
    } else {
      // Modem lost - reinitialize
      Serial.println("[MODEM] Connection lost during keepalive - reinitializing...");
      modemReady = false;
      initModem();
      lastModemKeepAlive = now;
    }
  }
}
