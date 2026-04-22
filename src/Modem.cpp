#include "Modem.h"
#include "Shared.h"
#include <HardwareSerial.h>
#include <freertos/queue.h>

HardwareSerial SerialAT(1);
bool modemReady = false;
static QueueHandle_t smsQueue = nullptr;

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

    if (response.length() > 0 && (millis() - lastByteAt) > 250) {
      break;
    }

    delay(10);
  }

  response.trim();
  return response;
}

static void updateModemState(int16_t modemState, int16_t simState, int16_t networkState) {
  Shared_writeInputRegister(MODEM_STATUS_REGISTER, modemState);
  Shared_writeInputRegister(SIM_STATUS_REGISTER, simState);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, networkState);
}

static bool enqueueJob(uint8_t messageIndex) {
  if (smsQueue == nullptr) {
    return false;
  }

  SmsJob job = {messageIndex};
  return xQueueSend(smsQueue, &job, 0) == pdTRUE;
}

static int16_t dispatchMessage(size_t messageIndex) {
  MessageConfig config = {};
  if (!Shared_getMessageConfig(messageIndex, config)) {
    return STATUS_ERROR_CONFIG;
  }

  if (config.phoneCount == 0) {
    return STATUS_ERROR_EMPTY;
  }

  Shared_writeInputRegister(MODEM_STATUS_REGISTER, STATE_BUSY);

  uint8_t sentCount = 0;
  for (size_t i = 0; i < PHONE_SLOTS_PER_MESSAGE; ++i) {
    if (config.phoneNumbers[i][0] == '\0') {
      continue;
    }

    if (sendSMS(String(config.phoneNumbers[i]), String(config.text))) {
      sentCount++;
    }
  }

  Shared_writeInputRegister(MODEM_STATUS_REGISTER, modemReady ? STATE_READY : STATE_ERROR);

  if (sentCount == 0) {
    return STATUS_ERROR_SEND;
  }

  return (int16_t)sentCount;
}

static void scanTriggerEdges(bool previousState[MESSAGE_SLOT_COUNT]) {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    bool current = snapshot.triggerRegs[i] != 0;
    if (current && !previousState[i]) {
      if (!enqueueJob((uint8_t)i)) {
        Shared_writeResultRegister(i, STATUS_ERROR_MODEM);
      }
    }
    previousState[i] = current;
  }
}

void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  Serial.println("[MODEM] Power ON triggered");
  delay(8000);
}

String sendAT(const String &cmd, int timeout) {
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

bool modemSimReady() {
  String sim = sendAT("AT+CPIN?", 2000);
  bool ready = sim.indexOf("READY") != -1;
  Shared_writeInputRegister(SIM_STATUS_REGISTER, ready ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM);
  return ready;
}

bool waitForNetwork() {
  for (int i = 0; i < 10; ++i) {
    String res = sendAT("AT+CREG?", 2000);
    if (res.indexOf("0,1") != -1 || res.indexOf("0,5") != -1) {
      Shared_writeInputRegister(NETWORK_STATUS_REGISTER, STATE_READY);
      Serial.println("[MODEM] Network Registered");
      return true;
    }

    Serial.println("[MODEM] Waiting for network...");
    delay(2000);
  }

  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, STATUS_ERROR_NETWORK);
  Serial.println("[MODEM] Network FAILED");
  return false;
}

bool sendSMS(const String &number, const String &message) {
  if (!modemReady) {
    Serial.println("[SMS] ERROR: Modem not ready");
    return false;
  }

  if (!modemSimReady()) {
    Serial.println("[SMS] SIM not ready");
    return false;
  }

  if (!waitForNetwork()) {
    Serial.println("[SMS] Network not available");
    return false;
  }

  sendAT("AT");
  sendAT("AT+CMEE=2", 2000);
  sendAT("AT+CSCS=\"GSM\"", 2000);
  sendAT("AT+CSMP=17,167,0,0", 2000);
  sendAT("AT+CMGF=1", 2000);

  Serial.printf("[SMS] Sending to %s\n", number.c_str());
  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");
  delay(1000);

  SerialAT.print(message);
  delay(500);
  SerialAT.write(26);

  String res = readSerialATResponse(15000);
  Serial.printf("[SMS] Modem response: %s\n", res.c_str());
  return res.indexOf("+CMGS:") != -1 && res.indexOf("ERROR") == -1;
}

void initModem() {
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  updateModemState(STATE_BUSY, STATE_UNKNOWN, STATE_UNKNOWN);

  Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
  modemPowerOn();

  String res = sendAT("AT", 2000);
  if (res.indexOf("OK") == -1) {
    delay(3000);
    res = sendAT("AT", 2000);
  }

  if (res.indexOf("OK") == -1) {
    modemReady = false;
    updateModemState(STATE_ERROR, STATE_UNKNOWN, STATE_UNKNOWN);
    Serial.println("[MODEM] No modem response after power ON");
    return;
  }

  sendAT("AT+CMEE=2", 2000);
  bool simReady = modemSimReady();
  bool networkReady = simReady && waitForNetwork();
  modemReady = simReady && networkReady;
  updateModemState(modemReady ? STATE_READY : STATE_ERROR,
                   simReady ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM,
                   networkReady ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_NETWORK);
  Serial.println(modemReady ? "[MODEM] Modem initialized successfully" : "[MODEM] Modem initialization failed");
}

bool Modem_init(size_t queueLength) {
  if (smsQueue == nullptr) {
    smsQueue = xQueueCreate(queueLength, sizeof(SmsJob));
  }

  Shared_writeInputRegister(DEVICE_STATUS_REGISTER, STATE_READY);
  Shared_writeInputRegister(MODEM_STATUS_REGISTER, STATE_UNKNOWN);
  Shared_writeInputRegister(SIM_STATUS_REGISTER, STATE_UNKNOWN);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, STATE_UNKNOWN);
  return smsQueue != nullptr;
}

void Modem_task(void *pvParameters) {
  (void)pvParameters;
  bool previousState[MESSAGE_SLOT_COUNT] = {};
  SmsJob job = {};

  for (;;) {
    scanTriggerEdges(previousState);

    if (xQueueReceive(smsQueue, &job, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (!modemReady) {
        initModem();
      }

      if (!modemReady) {
        Shared_writeResultRegister(job.messageIndex, STATUS_ERROR_MODEM);
        continue;
      }

      int16_t result = dispatchMessage(job.messageIndex);
      Shared_writeResultRegister(job.messageIndex, result);
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }
}
