/*
#include "Modem.h"
#include "Shared.h"
#include <HardwareSerial.h>
#include <freertos/queue.h>

HardwareSerial SerialAT(1);
bool modemReady = false;
static QueueHandle_t smsQueue = nullptr;

// ---------------------------------------------------------------------------
// Serial AT helpers
// ---------------------------------------------------------------------------
static String readSerialATResponse(unsigned long timeout) {
  String response = "";
  unsigned long start      = millis();
  unsigned long lastByteAt = start;

  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      response += (char)SerialAT.read();
      lastByteAt = millis();
      delay(2);
    }
    if (response.length() > 0 && (millis() - lastByteAt) > 100) break;
    delay(10);
  }

  response.trim();
  return response;
}

static void updateModemState(int16_t modemState, int16_t simState, int16_t networkState) {
  Shared_writeInputRegister(MODEM_STATUS_REGISTER,   modemState);
  Shared_writeInputRegister(SIM_STATUS_REGISTER,     simState);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, networkState);
}

// ---------------------------------------------------------------------------
// Queue helper
// ---------------------------------------------------------------------------
static bool enqueueJob(uint8_t messageIndex) {
  if (smsQueue == nullptr) return false;
  SmsJob job = {messageIndex};
  return xQueueSend(smsQueue, &job, 0) == pdTRUE;
}

// ---------------------------------------------------------------------------
// AT command
// ---------------------------------------------------------------------------
String sendAT(const String &cmd, int timeout) {
  while (SerialAT.available()) SerialAT.read();

  Serial.println("[AT] >> " + cmd);
  SerialAT.println(cmd);
  delay(100);

  String response = readSerialATResponse((unsigned long)timeout);
  Serial.println(response.length() ? "[AT] << " + response : "[AT] << [NO RESPONSE]");
  return response;
}

// ---------------------------------------------------------------------------
// Modem checks
// ---------------------------------------------------------------------------
bool modemSimReady() {
  String sim   = sendAT("AT+CPIN?", 2000);
  bool   ready = sim.indexOf("READY") != -1;
  Shared_writeInputRegister(SIM_STATUS_REGISTER,
    ready ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM);
  return ready;
}

bool waitForNetwork() {
  for (int i = 0; i < 10; ++i) {
    String res = sendAT("AT+CREG?", 2000);
    if (res.indexOf("0,1") != -1 || res.indexOf("0,5") != -1) {
      Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_READY);
      Serial.println("[MODEM] Network registered");
      return true;
    }
    Serial.println("[MODEM] Waiting for network...");
    delay(2000);
  }
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATUS_ERROR_NETWORK);
  Serial.println("[MODEM] Network FAILED");
  return false;
}

// ---------------------------------------------------------------------------
// sendSMS — two-step AT+CMGS exchange
// ---------------------------------------------------------------------------
bool sendSMS(const String &number, const String &message) {
  if (!modemReady) {
    Serial.println("[SMS] ERROR: Modem not ready");
    return false;
  }

  if (!modemSimReady()) {
    Serial.println("[SMS] SIM not ready — marking modem not ready");
    modemReady = false;
    return false;
  }

  String netRes  = sendAT("AT+CREG?", 2000);
  bool onNetwork = netRes.indexOf("0,1") != -1 || netRes.indexOf("0,5") != -1;
  if (!onNetwork) {
    Serial.println("[SMS] Network lost — marking modem not ready");
    modemReady = false;
    Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATUS_ERROR_NETWORK);
    return false;
  }

  sendAT("AT",                 1000);
  sendAT("AT+CMEE=2",          2000);
  sendAT("AT+CSCS=\"GSM\"",    2000);
  sendAT("AT+CSMP=17,167,0,0", 2000);
  sendAT("AT+CMGF=1",          2000);

  // Step 1 — send CMGS command, wait for ">" prompt
  Serial.printf("[SMS] Sending to %s\n", number.c_str());
  while (SerialAT.available()) SerialAT.read();

  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  String prompt = readSerialATResponse(5000);
  Serial.printf("[SMS] CMGS prompt: %s\n", prompt.c_str());

  if (prompt.indexOf(">") == -1) {
    Serial.println("[SMS] No '>' prompt received — aborting");
    SerialAT.write(26);
    delay(500);
    modemReady = false;
    return false;
  }

  // Step 2 — send message body then Ctrl+Z
  SerialAT.print(message);
  delay(200);
  SerialAT.write(26);

  String res = readSerialATResponse(15000);
  Serial.printf("[SMS] CMGS result: %s\n", res.c_str());

  bool ok = res.indexOf("+CMGS:") != -1 && res.indexOf("ERROR") == -1;
  if (!ok) {
    if (res.indexOf("ERROR") != -1) {
      // Explicit ERROR from modem (e.g. CMS ERROR: invalid number format)
      // Modem itself is fine — just log and return false
      Serial.println("[SMS] Send rejected by network/modem (bad number or format) — modem stays ready");
    } else {
      // Empty or unrecognised response — modem may be in a bad state
      Serial.println("[SMS] No confirmation received — marking modem not ready");
      modemReady = false;
    }
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Power on
// ---------------------------------------------------------------------------
static void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  Serial.println("[MODEM] Power ON triggered");
  delay(8000);
}

// ---------------------------------------------------------------------------
// initModem
// ---------------------------------------------------------------------------
void initModem() {
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  updateModemState((int16_t)STATE_BUSY, (int16_t)STATE_UNKNOWN, (int16_t)STATE_UNKNOWN);

  Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
  modemPowerOn();

  String res = sendAT("AT", 2000);
  if (res.indexOf("OK") == -1) {
    delay(3000);
    res = sendAT("AT", 2000);
  }

  if (res.indexOf("OK") == -1) {
    modemReady = false;
    updateModemState((int16_t)STATE_ERROR, (int16_t)STATE_UNKNOWN, (int16_t)STATE_UNKNOWN);
    Serial.println("[MODEM] No modem response after power ON");
    return;
  }

  sendAT("AT+CMEE=2", 2000);
  bool simOk     = modemSimReady();
  bool networkOk = simOk && waitForNetwork();
  modemReady     = simOk && networkOk;

  updateModemState(
    modemReady  ? (int16_t)STATE_READY : (int16_t)STATE_ERROR,
    simOk       ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM,
    networkOk   ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_NETWORK
  );

  Serial.println(modemReady
    ? "[MODEM] Modem initialized successfully"
    : "[MODEM] Modem initialization failed");
}

// ---------------------------------------------------------------------------
// Modem_init — called from setup()
// ---------------------------------------------------------------------------
bool Modem_init(size_t queueLength) {
  if (smsQueue == nullptr) {
    smsQueue = xQueueCreate(queueLength, sizeof(SmsJob));
  }
  Shared_writeInputRegister(DEVICE_STATUS_REGISTER,  (int16_t)STATE_READY);
  Shared_writeInputRegister(MODEM_STATUS_REGISTER,   (int16_t)STATE_UNKNOWN);
  Shared_writeInputRegister(SIM_STATUS_REGISTER,     (int16_t)STATE_UNKNOWN);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_UNKNOWN);
  return smsQueue != nullptr;
}

// ---------------------------------------------------------------------------
// dispatchMessage
// ---------------------------------------------------------------------------
static int16_t dispatchMessage(size_t messageIndex) {
  MessageConfig config = {};
  if (!Shared_getMessageConfig(messageIndex, config)) return STATUS_ERROR_CONFIG;
  if (config.phoneCount == 0)                         return STATUS_ERROR_EMPTY;

  Shared_writeInputRegister(MODEM_STATUS_REGISTER, (int16_t)STATE_BUSY);

  uint8_t sentCount = 0;
  for (size_t i = 0; i < PHONE_SLOTS_PER_MESSAGE; ++i) {
    if (config.phoneNumbers[i][0] == '\0') continue;
    if (sendSMS(String(config.phoneNumbers[i]), String(config.text))) sentCount++;
  }

  Shared_writeInputRegister(MODEM_STATUS_REGISTER,
    modemReady ? (int16_t)STATE_READY : (int16_t)STATE_ERROR);

  if (sentCount == 0) return STATUS_ERROR_SEND;
  return (int16_t)sentCount;
}

// ---------------------------------------------------------------------------
// Rising-edge scanner
//
// pendingJobs[] — per-slot flag that is set when a job is enqueued and
// cleared when the job is processed. This guarantees a slot can never have
// more than one job in the queue at a time, even if scanTriggerEdges runs
// multiple times while the register stays at 1.
//
// Clear-on-zero — when the PLC writes 0 to a trigger register, the
// corresponding result register is cleared back to STATUS_IDLE (0).
// This resets the slot so the PLC can write 1 again to retrigger.
// ---------------------------------------------------------------------------
static bool pendingJobs[MESSAGE_SLOT_COUNT] = {};

static void scanTriggerEdges(bool previousState[MESSAGE_SLOT_COUNT]) {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    bool current = snapshot.triggerRegs[i] != 0;

    if (current && !previousState[i] && !pendingJobs[i]) {
      // Rising edge detected AND no job already queued for this slot
      if (enqueueJob((uint8_t)i)) {
        pendingJobs[i] = true;
      } else {
        Shared_writeResultRegister(i, STATUS_ERROR_MODEM);
      }
    }

    if (!current && previousState[i]) {
      // Falling edge (PLC wrote 0) — clear result register back to idle
      // so the PLC gets a clean slate before retriggering
      Shared_writeResultRegister(i, STATUS_IDLE);
      Serial.printf("[EDGE] Slot %u cleared (trigger -> 0)\n", (unsigned)i);
    }

    previousState[i] = current;
  }
}

// ---------------------------------------------------------------------------
// Modem_task — 4-task version
// initModem() runs at the top of the task on core 0.
// Core 1 (RTU, TCP, AP) runs independently during the ~15s init.
// No semaphore needed — nothing on core 0 competes with SmsTask.
// ---------------------------------------------------------------------------
void Modem_task(void *pvParameters) {
  (void)pvParameters;

  // Blocking init — only affects core 0, core 1 tasks run freely
  initModem();
  Serial.println("[MODEM TASK] Init complete, starting SMS processing");

  bool   previousState[MESSAGE_SLOT_COUNT] = {};
  SmsJob job = {};

  for (;;) {
    // Scan edges first so we catch any transitions that happened
    // while the previous dispatch was running
    scanTriggerEdges(previousState);

    if (xQueueReceive(smsQueue, &job, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (!modemReady) {
        Serial.println("[MODEM] Not ready — attempting reinit...");
        initModem();
      }

      if (!modemReady) {
        Shared_writeResultRegister(job.messageIndex, STATUS_ERROR_MODEM);
        pendingJobs[job.messageIndex] = false;  // allow retry on next trigger
        continue;
      }

      int16_t result = dispatchMessage(job.messageIndex);
      Shared_writeResultRegister(job.messageIndex, result);
      pendingJobs[job.messageIndex] = false;  // slot is free for next trigger
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

*/
// ############################################################################################

#include "Modem.h"
#include "Shared.h"
#include <HardwareSerial.h>
#include <freertos/queue.h>

HardwareSerial SerialAT(1);
bool modemReady = false;
static QueueHandle_t smsQueue = nullptr;

// ---------------------------------------------------------------------------
// Serial AT helpers
// ---------------------------------------------------------------------------
static String readSerialATResponse(unsigned long timeout) {
  String response = "";
  unsigned long start      = millis();
  unsigned long lastByteAt = start;

  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      response += (char)SerialAT.read();
      lastByteAt = millis();
      delay(2);
    }
    if (response.length() > 0 && (millis() - lastByteAt) > 100) break;
    delay(10);
  }

  response.trim();
  return response;
}

static void updateModemState(int16_t modemState, int16_t simState, int16_t networkState) {
  Shared_writeInputRegister(MODEM_STATUS_REGISTER,   modemState);
  Shared_writeInputRegister(SIM_STATUS_REGISTER,     simState);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, networkState);
}

// ---------------------------------------------------------------------------
// Queue helper
// ---------------------------------------------------------------------------
static bool enqueueJob(uint8_t messageIndex) {
  if (smsQueue == nullptr) return false;
  SmsJob job = {messageIndex};
  return xQueueSend(smsQueue, &job, 0) == pdTRUE;
}

// ---------------------------------------------------------------------------
// AT command
// ---------------------------------------------------------------------------
String sendAT(const String &cmd, int timeout) {
  while (SerialAT.available()) SerialAT.read();

  Serial.println("[AT] >> " + cmd);
  SerialAT.println(cmd);
  delay(100);

  String response = readSerialATResponse((unsigned long)timeout);
  Serial.println(response.length() ? "[AT] << " + response : "[AT] << [NO RESPONSE]");
  return response;
}

// ---------------------------------------------------------------------------
// Modem checks
// ---------------------------------------------------------------------------
bool modemSimReady() {
  String sim   = sendAT("AT+CPIN?", 2000);
  bool   ready = sim.indexOf("READY") != -1;
  Shared_writeInputRegister(SIM_STATUS_REGISTER,
    ready ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM);
  return ready;
}

bool waitForNetwork() {
  for (int i = 0; i < 10; ++i) {
    String res = sendAT("AT+CREG?", 2000);
    if (res.indexOf("0,1") != -1 || res.indexOf("0,5") != -1) {
      Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_READY);
      Serial.println("[MODEM] Network registered");
      return true;
    }
    Serial.println("[MODEM] Waiting for network...");
    delay(2000);
  }
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATUS_ERROR_NETWORK);
  Serial.println("[MODEM] Network FAILED");
  return false;
}

// ---------------------------------------------------------------------------
// sendSMS — two-step AT+CMGS exchange
// ---------------------------------------------------------------------------
bool sendSMS(const String &number, const String &message) {
  if (!modemReady) {
    Serial.println("[SMS] ERROR: Modem not ready");
    return false;
  }

  if (!modemSimReady()) {
    Serial.println("[SMS] SIM not ready — marking modem not ready");
    modemReady = false;
    return false;
  }

  String netRes  = sendAT("AT+CREG?", 2000);
  bool onNetwork = netRes.indexOf("0,1") != -1 || netRes.indexOf("0,5") != -1;
  if (!onNetwork) {
    Serial.println("[SMS] Network lost — marking modem not ready");
    modemReady = false;
    Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATUS_ERROR_NETWORK);
    return false;
  }

  sendAT("AT",                 1000);
  sendAT("AT+CMEE=2",          2000);
  sendAT("AT+CSCS=\"GSM\"",    2000);
  sendAT("AT+CSMP=17,167,0,0", 2000);
  sendAT("AT+CMGF=1",          2000);

  // Step 1 — send CMGS command, wait for ">" prompt
  Serial.printf("[SMS] Sending to %s\n", number.c_str());
  while (SerialAT.available()) SerialAT.read();

  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  String prompt = readSerialATResponse(5000);
  Serial.printf("[SMS] CMGS prompt: %s\n", prompt.c_str());

  if (prompt.indexOf(">") == -1) {
    Serial.println("[SMS] No '>' prompt received — aborting");
    SerialAT.write(26);
    delay(500);
    modemReady = false;
    return false;
  }

  // Step 2 — send message body then Ctrl+Z
  SerialAT.print(message);
  delay(200);
  SerialAT.write(26);

  String res = readSerialATResponse(15000);
  Serial.printf("[SMS] CMGS result: %s\n", res.c_str());

  bool ok = res.indexOf("+CMGS:") != -1 && res.indexOf("ERROR") == -1;
  if (!ok) {
    // Delivery failed — network rejected this number or no confirmation received.
    // The modem may be left in a hung state after the failed CMGS exchange.
    // Send Ctrl+Z + ESC to cancel any pending state, then ping with AT to
    // confirm the modem is responsive before returning.
    Serial.println("[SMS] Delivery failed for this number — flushing modem state");
    SerialAT.write(26);   // Ctrl+Z — cancel any pending CMGS
    delay(300);
    SerialAT.write(27);   // ESC — additional abort
    delay(300);
    while (SerialAT.available()) SerialAT.read();  // flush rx buffer

    // Quick ping to confirm modem is back to command mode
    String pingRes = sendAT("AT", 2000);
    if (pingRes.indexOf("OK") == -1) {
      Serial.println("[SMS] Modem unresponsive after flush — marking not ready");
      modemReady = false;
    } else {
      Serial.println("[SMS] Modem recovered — continuing to next number");
    }
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Power on
// ---------------------------------------------------------------------------
static void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  Serial.println("[MODEM] Power ON triggered");
  delay(8000);
}

// ---------------------------------------------------------------------------
// initModem
// ---------------------------------------------------------------------------
void initModem() {
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  updateModemState((int16_t)STATE_BUSY, (int16_t)STATE_UNKNOWN, (int16_t)STATE_UNKNOWN);

  Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
  modemPowerOn();

  String res = sendAT("AT", 2000);
  if (res.indexOf("OK") == -1) {
    delay(3000);
    res = sendAT("AT", 2000);
  }

  if (res.indexOf("OK") == -1) {
    modemReady = false;
    updateModemState((int16_t)STATE_ERROR, (int16_t)STATE_UNKNOWN, (int16_t)STATE_UNKNOWN);
    Serial.println("[MODEM] No modem response after power ON");
    return;
  }

  sendAT("AT+CMEE=2", 2000);
  bool simOk     = modemSimReady();
  bool networkOk = simOk && waitForNetwork();
  modemReady     = simOk && networkOk;

  updateModemState(
    modemReady  ? (int16_t)STATE_READY : (int16_t)STATE_ERROR,
    simOk       ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM,
    networkOk   ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_NETWORK
  );

  Serial.println(modemReady
    ? "[MODEM] Modem initialized successfully"
    : "[MODEM] Modem initialization failed");
}

// ---------------------------------------------------------------------------
// Modem_init — called from setup()
// ---------------------------------------------------------------------------
bool Modem_init(size_t queueLength) {
  if (smsQueue == nullptr) {
    smsQueue = xQueueCreate(queueLength, sizeof(SmsJob));
  }
  Shared_writeInputRegister(DEVICE_STATUS_REGISTER,  (int16_t)STATE_READY);
  Shared_writeInputRegister(MODEM_STATUS_REGISTER,   (int16_t)STATE_UNKNOWN);
  Shared_writeInputRegister(SIM_STATUS_REGISTER,     (int16_t)STATE_UNKNOWN);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_UNKNOWN);
  return smsQueue != nullptr;
}

// ---------------------------------------------------------------------------
// dispatchMessage
// ---------------------------------------------------------------------------
static int16_t dispatchMessage(size_t messageIndex) {
  MessageConfig config = {};
  if (!Shared_getMessageConfig(messageIndex, config)) return STATUS_ERROR_CONFIG;
  if (config.phoneCount == 0)                         return STATUS_ERROR_EMPTY;

  Shared_writeInputRegister(MODEM_STATUS_REGISTER, (int16_t)STATE_BUSY);

  uint8_t sentCount = 0;
  for (size_t i = 0; i < PHONE_SLOTS_PER_MESSAGE; ++i) {
    if (config.phoneNumbers[i][0] == '\0') continue;

    // If modem became unresponsive mid-dispatch (not just a bad number),
    // skip remaining numbers — Modem_task will reinit on the next job
    if (!modemReady) {
      Serial.printf("[SMS] Modem not ready — skipping remaining numbers from slot %u\n", (unsigned)i);
      break;
    }

    if (sendSMS(String(config.phoneNumbers[i]), String(config.text))) {
      sentCount++;
    }
  }

  Shared_writeInputRegister(MODEM_STATUS_REGISTER,
    modemReady ? (int16_t)STATE_READY : (int16_t)STATE_ERROR);

  if (sentCount == 0) return STATUS_ERROR_SEND;
  return (int16_t)sentCount;
}

// ---------------------------------------------------------------------------
// Rising-edge scanner
//
// pendingJobs[] — per-slot flag that is set when a job is enqueued and
// cleared when the job is processed. This guarantees a slot can never have
// more than one job in the queue at a time, even if scanTriggerEdges runs
// multiple times while the register stays at 1.
//
// Clear-on-zero — when the PLC writes 0 to a trigger register, the
// corresponding result register is cleared back to STATUS_IDLE (0).
// This resets the slot so the PLC can write 1 again to retrigger.
// ---------------------------------------------------------------------------
static bool pendingJobs[MESSAGE_SLOT_COUNT] = {};

static void scanTriggerEdges(bool previousState[MESSAGE_SLOT_COUNT]) {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    bool current = snapshot.triggerRegs[i] != 0;

    if (current && !previousState[i] && !pendingJobs[i]) {
      // Rising edge detected AND no job already queued for this slot
      if (enqueueJob((uint8_t)i)) {
        pendingJobs[i] = true;
      } else {
        Shared_writeResultRegister(i, STATUS_ERROR_MODEM);
      }
    }

    if (!current && previousState[i]) {
      // Falling edge (PLC wrote 0) — clear result register back to idle
      // so the PLC gets a clean slate before retriggering
      Shared_writeResultRegister(i, STATUS_IDLE);
      Serial.printf("[EDGE] Slot %u cleared (trigger -> 0)\n", (unsigned)i);
    }

    previousState[i] = current;
  }
}

// ---------------------------------------------------------------------------
// Modem_task — 4-task version
// initModem() runs at the top of the task on core 0.
// Core 1 (RTU, TCP, AP) runs independently during the ~15s init.
// No semaphore needed — nothing on core 0 competes with SmsTask.
// ---------------------------------------------------------------------------
void Modem_task(void *pvParameters) {
  (void)pvParameters;

  // Blocking init — only affects core 0, core 1 tasks run freely
  initModem();
  Serial.println("[MODEM TASK] Init complete, starting SMS processing");

  bool   previousState[MESSAGE_SLOT_COUNT] = {};
  SmsJob job = {};

  for (;;) {
    // Scan edges first so we catch any transitions that happened
    // while the previous dispatch was running
    scanTriggerEdges(previousState);

    if (xQueueReceive(smsQueue, &job, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (!modemReady) {
        Serial.println("[MODEM] Not ready — attempting reinit...");
        initModem();
      }

      if (!modemReady) {
        Shared_writeResultRegister(job.messageIndex, STATUS_ERROR_MODEM);
        pendingJobs[job.messageIndex] = false;  // allow retry on next trigger
        continue;
      }

      int16_t result = dispatchMessage(job.messageIndex);
      Shared_writeResultRegister(job.messageIndex, result);
      pendingJobs[job.messageIndex] = false;  // slot is free for next trigger
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }
}
