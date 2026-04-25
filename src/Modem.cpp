// // =============================================================================
// // Modem.cpp  —  OPTION A: Boot-time initialisation
// // =============================================================================
// // initModem() is called from setup() BEFORE tasks are created.
// // The modem is fully ready (or has failed) before any FreeRTOS task runs.
// //
// // Pros:  Simple. No reinit complexity. Tasks start clean.
// // Cons:  Boot takes ~10-15 s while modem powers up and registers on network.
// // =============================================================================

// #include "Modem.h"
// #include "Shared.h"
// #include <HardwareSerial.h>
// #include <freertos/queue.h>

// HardwareSerial SerialAT(1);
// bool modemReady = false;
// static QueueHandle_t smsQueue = nullptr;

// // ---------------------------------------------------------------------------
// // Serial AT helpers
// // ---------------------------------------------------------------------------
// static String readSerialATResponse(unsigned long timeout) {
//   String response = "";
//   unsigned long start     = millis();
//   unsigned long lastByteAt = start;

//   while (millis() - start < timeout) {
//     while (SerialAT.available()) {
//       response += (char)SerialAT.read();
//       lastByteAt = millis();
//       delay(2);
//     }
//     // 100 ms idle window — enough for most modems, faster than the old 250 ms
//     if (response.length() > 0 && (millis() - lastByteAt) > 100) break;
//     delay(10);
//   }

//   response.trim();
//   return response;
// }

// static void updateModemState(int16_t modemState, int16_t simState, int16_t networkState) {
//   Shared_writeInputRegister(MODEM_STATUS_REGISTER,   modemState);
//   Shared_writeInputRegister(SIM_STATUS_REGISTER,     simState);
//   Shared_writeInputRegister(NETWORK_STATUS_REGISTER, networkState);
// }

// // ---------------------------------------------------------------------------
// // Queue helper
// // ---------------------------------------------------------------------------
// static bool enqueueJob(uint8_t messageIndex) {
//   if (smsQueue == nullptr) return false;
//   SmsJob job = {messageIndex};
//   return xQueueSend(smsQueue, &job, 0) == pdTRUE;
// }

// // ---------------------------------------------------------------------------
// // AT command
// // ---------------------------------------------------------------------------
// String sendAT(const String &cmd, int timeout) {
//   while (SerialAT.available()) SerialAT.read();  // flush

//   Serial.println("[AT] >> " + cmd);
//   SerialAT.println(cmd);
//   delay(100);

//   String response = readSerialATResponse((unsigned long)timeout);
//   Serial.println(response.length() ? "[AT] << " + response : "[AT] << [NO RESPONSE]");
//   return response;
// }

// // ---------------------------------------------------------------------------
// // Modem checks  (used during init and per-SMS)
// // ---------------------------------------------------------------------------
// bool modemSimReady() {
//   String sim   = sendAT("AT+CPIN?", 2000);
//   bool   ready = sim.indexOf("READY") != -1;
//   Shared_writeInputRegister(SIM_STATUS_REGISTER,
//     ready ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM);
//   return ready;
// }

// bool waitForNetwork() {
//   for (int i = 0; i < 10; ++i) {
//     String res = sendAT("AT+CREG?", 2000);
//     if (res.indexOf("0,1") != -1 || res.indexOf("0,5") != -1) {
//       Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_READY);
//       Serial.println("[MODEM] Network registered");
//       return true;
//     }
//     Serial.println("[MODEM] Waiting for network...");
//     delay(2000);
//   }
//   Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATUS_ERROR_NETWORK);
//   Serial.println("[MODEM] Network FAILED");
//   return false;
// }

// // ---------------------------------------------------------------------------
// // sendSMS  — does NOT call waitForNetwork() inline.
// // Network check is done once in initModem() at boot.
// // If the network drops mid-session, modemReady is set false and the task
// // will call initModem() again on the next job.
// // ---------------------------------------------------------------------------
// bool sendSMS(const String &number, const String &message) {
//   if (!modemReady) {
//     Serial.println("[SMS] ERROR: Modem not ready");
//     return false;
//   }

//   // Quick SIM check before each SMS (cheap, ~200 ms)
//   if (!modemSimReady()) {
//     Serial.println("[SMS] SIM not ready — marking modem not ready");
//     modemReady = false;
//     return false;
//   }

//   // Quick network check (single poll, not the full 10-retry loop)
//   String netRes = sendAT("AT+CREG?", 2000);
//   bool onNetwork = netRes.indexOf("0,1") != -1 || netRes.indexOf("0,5") != -1;
//   if (!onNetwork) {
//     Serial.println("[SMS] Network lost — marking modem not ready");
//     modemReady = false;
//     Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATUS_ERROR_NETWORK);
//     return false;
//   }

//   sendAT("AT",              1000);
//   sendAT("AT+CMEE=2",       2000);
//   sendAT("AT+CSCS=\"GSM\"", 2000);
//   sendAT("AT+CSMP=17,167,0,0", 2000);
//   sendAT("AT+CMGF=1",       2000);

//   Serial.printf("[SMS] Sending to %s\n", number.c_str());
//   SerialAT.print("AT+CMGS=\"");
//   SerialAT.print(number);
//   SerialAT.println("\"");
//   delay(1000);

//   SerialAT.print(message);
//   delay(500);
//   SerialAT.write(26);  // Ctrl+Z

//   String res = readSerialATResponse(15000);
//   Serial.printf("[SMS] Modem response: %s\n", res.c_str());

//   bool ok = res.indexOf("+CMGS:") != -1 && res.indexOf("ERROR") == -1;
//   if (!ok) {
//     // Hard send failure — flag modem for reinit next job
//     modemReady = false;
//   }
//   return ok;
// }

// // ---------------------------------------------------------------------------
// // Power on
// // ---------------------------------------------------------------------------
// static void modemPowerOn() {
//   pinMode(MODEM_PWRKEY, OUTPUT);
//   digitalWrite(MODEM_PWRKEY, LOW);
//   delay(1000);
//   digitalWrite(MODEM_PWRKEY, HIGH);
//   Serial.println("[MODEM] Power ON triggered");
//   delay(8000);
// }

// // ---------------------------------------------------------------------------
// // initModem  — called from setup() in OPTION A
// // ---------------------------------------------------------------------------
// void initModem() {
//   SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
//   delay(500);

//   updateModemState((int16_t)STATE_BUSY, (int16_t)STATE_UNKNOWN, (int16_t)STATE_UNKNOWN);

//   Serial.println("\n=== Initializing 4G Modem (EC200U) ===");
//   modemPowerOn();

//   String res = sendAT("AT", 2000);
//   if (res.indexOf("OK") == -1) {
//     delay(3000);
//     res = sendAT("AT", 2000);
//   }

//   if (res.indexOf("OK") == -1) {
//     modemReady = false;
//     updateModemState((int16_t)STATE_ERROR, (int16_t)STATE_UNKNOWN, (int16_t)STATE_UNKNOWN);
//     Serial.println("[MODEM] No modem response after power ON");
//     return;
//   }

//   sendAT("AT+CMEE=2", 2000);
//   bool simOk     = modemSimReady();
//   bool networkOk = simOk && waitForNetwork();
//   modemReady     = simOk && networkOk;

//   updateModemState(
//     modemReady  ? (int16_t)STATE_READY : (int16_t)STATE_ERROR,
//     simOk       ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_SIM,
//     networkOk   ? (int16_t)STATE_READY : (int16_t)STATUS_ERROR_NETWORK
//   );

//   Serial.println(modemReady
//     ? "[MODEM] Modem initialized successfully"
//     : "[MODEM] Modem initialization failed");
// }

// // ---------------------------------------------------------------------------
// // Queue & input register init  (called from setup() before tasks)
// // ---------------------------------------------------------------------------
// bool Modem_init(size_t queueLength) {
//   if (smsQueue == nullptr) {
//     smsQueue = xQueueCreate(queueLength, sizeof(SmsJob));
//   }
//   Shared_writeInputRegister(DEVICE_STATUS_REGISTER,  (int16_t)STATE_READY);
//   Shared_writeInputRegister(MODEM_STATUS_REGISTER,   (int16_t)STATE_UNKNOWN);
//   Shared_writeInputRegister(SIM_STATUS_REGISTER,     (int16_t)STATE_UNKNOWN);
//   Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_UNKNOWN);
//   return smsQueue != nullptr;
// }

// // ---------------------------------------------------------------------------
// // dispatchMessage
// // ---------------------------------------------------------------------------
// static int16_t dispatchMessage(size_t messageIndex) {
//   MessageConfig config = {};
//   if (!Shared_getMessageConfig(messageIndex, config)) return STATUS_ERROR_CONFIG;
//   if (config.phoneCount == 0)                         return STATUS_ERROR_EMPTY;

//   Shared_writeInputRegister(MODEM_STATUS_REGISTER, (int16_t)STATE_BUSY);

//   uint8_t sentCount = 0;
//   for (size_t i = 0; i < PHONE_SLOTS_PER_MESSAGE; ++i) {
//     if (config.phoneNumbers[i][0] == '\0') continue;
//     if (sendSMS(String(config.phoneNumbers[i]), String(config.text))) sentCount++;
//   }

//   Shared_writeInputRegister(MODEM_STATUS_REGISTER,
//     modemReady ? (int16_t)STATE_READY : (int16_t)STATE_ERROR);

//   if (sentCount == 0) return STATUS_ERROR_SEND;
//   return (int16_t)sentCount;
// }

// // ---------------------------------------------------------------------------
// // Rising-edge scanner
// // ---------------------------------------------------------------------------
// static void scanTriggerEdges(bool previousState[MESSAGE_SLOT_COUNT]) {
//   SystemSnapshot snapshot = Shared_getSnapshot();

//   for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
//     bool current = snapshot.triggerRegs[i] != 0;
//     if (current && !previousState[i]) {
//       if (!enqueueJob((uint8_t)i)) {
//         Shared_writeResultRegister(i, STATUS_ERROR_MODEM);
//       }
//     }
//     previousState[i] = current;
//   }
// }

// // ---------------------------------------------------------------------------
// // Modem task
// // In OPTION A modemReady is already set by initModem() in setup().
// // If a send error clears modemReady, the task calls initModem() to recover.
// // ---------------------------------------------------------------------------
// void Modem_task(void *pvParameters) {
//   (void)pvParameters;
//   bool   previousState[MESSAGE_SLOT_COUNT] = {};
//   SmsJob job = {};

//   for (;;) {
//     scanTriggerEdges(previousState);

//     if (xQueueReceive(smsQueue, &job, pdMS_TO_TICKS(50)) == pdTRUE) {
//       if (!modemReady) {
//         Serial.println("[MODEM] Not ready — attempting reinit...");
//         initModem();
//       }

//       if (!modemReady) {
//         Shared_writeResultRegister(job.messageIndex, STATUS_ERROR_MODEM);
//         continue;
//       }

//       int16_t result = dispatchMessage(job.messageIndex);
//       Shared_writeResultRegister(job.messageIndex, result);
//     }

//     vTaskDelay(pdMS_TO_TICKS(25));
//   }
// }

// rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
// configsip: 0, SPIWP:0xee
// clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
// mode:DIO, clock div:2
// load:0x3fff0030,len:1184
// load:0x40078000,len:13232
// load:0x40080400,len:3028
// entry 0x400805e4

// === MB Map RTOS SMS Controller (Option A — boot-time init) ===
// [MBMAP] Loaded 5 message entries from MBmapconf.csv

// === AP Mode Info ===
// To enable AP Mode: Press and hold button on GPIO 33
// AP SSID: ESP32_FileServer
// AP Password: 12345678
// AP URL: http://192.168.4.1
// Note: AP mode not active by default
// [ETH] Starting Ethernet...
// [ETH] DHCP OK
// [ETH] IP: 192.168.8.108
// [ETH] Subnet: 255.255.255.0
// [ETH] Gateway: 192.168.8.1
// [ETH] Modbus TCP Port: 502
// [ETH] Modbus TCP server ready

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [MODEM] Network registered
// [MODEM] Modem initialized successfully
// [SYSTEM] Tasks started: RTUTask, TCPTask, SmsTask, ApTask
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CSCS="GSM"
// [AT] << OK
// [AT] >> AT+CSMP=17,167,0,0
// [AT] << OK
// [AT] >> AT+CMGF=1
// [AT] << OK
// [SMS] Sending to 8149979689
// [SMS] Modem response: >
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [MODEM] Not ready — attempting reinit...

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << [NO RESPONSE]
// [AT] >> AT
// [AT] << [NO RESPONSE]
// [MODEM] No modem response after power ON
// [MODEM] Not ready — attempting reinit...

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,2

// OK
// [MODEM] Waiting for network...
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [MODEM] Network registered
// [MODEM] Modem initialized successfully
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CSCS="GSM"
// [AT] << OK
// [AT] >> AT+CSMP=17,167,0,0
// [AT] << OK
// [AT] >> AT+CMGF=1
// [AT] << OK
// [SMS] Sending to 8149979689
// [SMS] Modem response: >
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [MODEM] Not ready — attempting reinit...

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [MODEM] Network registered
// [MODEM] Modem initialized successfully
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CSCS="GSM"
// [AT] << OK
// [AT] >> AT+CSMP=17,167,0,0
// [AT] << OK
// [AT] >> AT+CMGF=1
// [AT] << OK
// [SMS] Sending to 8149979689
// [SMS] Modem response: >
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [MODEM] Not ready — attempting reinit...

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << [NO RESPONSE]
// [AT] >> AT
// [AT] << [NO RESPONSE]
// [MODEM] No modem response after power ON
// [MODEM] Not ready — attempting reinit...

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,2

// OK
// [MODEM] Waiting for network...
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [MODEM] Network registered
// [MODEM] Modem initialized successfully
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CSCS="GSM"
// [AT] << OK
// [AT] >> AT+CSMP=17,167,0,0
// [AT] << OK
// [AT] >> AT+CMGF=1
// [AT] << OK
// [SMS] Sending to 8149979689
// [SMS] Modem response: >
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [MODEM] Not ready — attempting reinit...

// === Initializing 4G Modem (EC200U) ===
// [MODEM] Power ON triggered
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [MODEM] Network registered
// [MODEM] Modem initialized successfully
// [AT] >> AT+CPIN?
// [AT] << +CPIN: READY

// OK
// [AT] >> AT+CREG?
// [AT] << +CREG: 0,1

// OK
// [AT] >> AT
// [AT] << OK
// [AT] >> AT+CMEE=2
// [AT] << OK
// [AT] >> AT+CSCS="GSM"
// [AT] << OK
// [AT] >> AT+CSMP=17,167,0,0
// [AT] << OK
// [AT] >> AT+CMGF=1
// [AT] << OK
// [SMS] Sending to 8149979689
// [SMS] Modem response: >
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [SMS] ERROR: Modem not ready
// [MODEM] Not ready — attempting reinit...


// =============================================================================
// Modem.cpp  —  OPTION B: Lazy initialisation (clean version)
// =============================================================================
// initModem() is NOT called from setup(). Instead it is called once from a
// dedicated one-shot init task that runs on core 0 immediately after task
// creation. This means:
//   - setup() returns fast (no 10-15 s boot wait).
//   - RTU/TCP tasks start accepting Modbus frames right away.
//   - The modem initialises in the background on core 0.
//   - Modem_task only starts processing SMS jobs after init completes.
//
// Pros:  Fast boot. PLC can connect immediately.
// Cons:  First SMS may be delayed if it arrives before init finishes.
//        Slightly more complex (uses a binary semaphore as a "ready gate").
//
// Usage in main.cpp:
//   Replace the single Modem_task creation with:
//     xTaskCreatePinnedToCore(Modem_initTask, "ModemInit", 4096, nullptr, 4, nullptr, 0);
//     xTaskCreatePinnedToCore(Modem_task,     "SmsTask",   8192, nullptr, 2, nullptr, 0);
//   The initTask deletes itself when done. Modem_task blocks on the semaphore.
// =============================================================================

// =============================================================================
// Modem.cpp  —  OPTION B: Lazy initialisation (clean version)
// =============================================================================
// initModem() is NOT called from setup(). Instead it is called once from a
// dedicated one-shot init task that runs on core 0 immediately after task
// creation. This means:
//   - setup() returns fast (no 10-15 s boot wait).
//   - RTU/TCP tasks start accepting Modbus frames right away.
//   - The modem initialises in the background on core 0.
//   - Modem_task only starts processing SMS jobs after init completes.
//
// Pros:  Fast boot. PLC can connect immediately.
// Cons:  First SMS may be delayed if it arrives before init finishes.
//        Slightly more complex (uses a binary semaphore as a "ready gate").
//
// Usage in main.cpp:
//   Replace the single Modem_task creation with:
//     xTaskCreatePinnedToCore(Modem_initTask, "ModemInit", 4096, nullptr, 4, nullptr, 0);
//     xTaskCreatePinnedToCore(Modem_task,     "SmsTask",   8192, nullptr, 2, nullptr, 0);
//   The initTask deletes itself when done. Modem_task blocks on the semaphore.
// =============================================================================

#include "Modem.h"
#include "Shared.h"
#include <HardwareSerial.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

HardwareSerial SerialAT(1);
bool modemReady = false;
static QueueHandle_t   smsQueue     = nullptr;
static SemaphoreHandle_t modemInitDone = nullptr;  // binary semaphore, given when init completes

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
// sendSMS  — single network poll, no blocking retry loop
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

  sendAT("AT",                  1000);
  sendAT("AT+CMEE=2",           2000);
  sendAT("AT+CSCS=\"GSM\"",     2000);
  sendAT("AT+CSMP=17,167,0,0",  2000);
  sendAT("AT+CMGF=1",           2000);

  // -------------------------------------------------------------------------
  // AT+CMGS is a two-step exchange:
  //   Step 1: Send AT+CMGS="number" → modem replies with ">" prompt
  //   Step 2: Send message text + Ctrl+Z → modem replies with +CMGS: <ref>
  //
  // We must wait for ">" before sending the body. If we send the body
  // without seeing ">", the modem ignores it and we never get +CMGS:.
  // -------------------------------------------------------------------------
  Serial.printf("[SMS] Sending to %s\n", number.c_str());

  // Flush any stale bytes
  while (SerialAT.available()) SerialAT.read();

  // Step 1 — send the CMGS command and wait for the ">" prompt
  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  // Wait up to 5 s for the ">" prompt
  String prompt = readSerialATResponse(5000);
  Serial.printf("[SMS] CMGS prompt: %s\n", prompt.c_str());

  if (prompt.indexOf(">") == -1) {
    Serial.println("[SMS] No '>' prompt received — aborting");
    // Send Ctrl+Z to cancel any pending CMGS state in the modem
    SerialAT.write(26);
    delay(500);
    modemReady = false;
    return false;
  }

  // Step 2 — send message body then Ctrl+Z
  SerialAT.print(message);
  delay(200);
  SerialAT.write(26);  // Ctrl+Z triggers actual send

  // Wait up to 15 s for the +CMGS: confirmation
  String res = readSerialATResponse(15000);
  Serial.printf("[SMS] CMGS result: %s\n", res.c_str());

  bool ok = res.indexOf("+CMGS:") != -1 && res.indexOf("ERROR") == -1;
  if (!ok) {
    Serial.println("[SMS] Send failed — marking modem not ready");
    modemReady = false;
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
  if (modemInitDone == nullptr) {
    modemInitDone = xSemaphoreCreateBinary();
  }
  Shared_writeInputRegister(DEVICE_STATUS_REGISTER,  (int16_t)STATE_READY);
  Shared_writeInputRegister(MODEM_STATUS_REGISTER,   (int16_t)STATE_UNKNOWN);
  Shared_writeInputRegister(SIM_STATUS_REGISTER,     (int16_t)STATE_UNKNOWN);
  Shared_writeInputRegister(NETWORK_STATUS_REGISTER, (int16_t)STATE_UNKNOWN);
  return smsQueue != nullptr && modemInitDone != nullptr;
}

// ---------------------------------------------------------------------------
// Modem_initTask — one-shot background init task (OPTION B only)
// Create this task in main.cpp at priority 4 on core 0 BEFORE Modem_task.
// It runs initModem(), signals the semaphore, then deletes itself.
// ---------------------------------------------------------------------------
void Modem_initTask(void *pvParameters) {
  (void)pvParameters;
  initModem();
  if (modemInitDone != nullptr) xSemaphoreGive(modemInitDone);
  vTaskDelete(nullptr);  // self-delete
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
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Modem_task
// Blocks on modemInitDone semaphore until Modem_initTask signals it.
// After that, runs the normal scan + dispatch loop.
// ---------------------------------------------------------------------------
void Modem_task(void *pvParameters) {
  (void)pvParameters;

  // Wait for background init to complete (no timeout — init must finish)
  Serial.println("[MODEM TASK] Waiting for modem init...");
  if (modemInitDone != nullptr) {
    xSemaphoreTake(modemInitDone, portMAX_DELAY);
  }
  Serial.println("[MODEM TASK] Init complete, starting SMS processing");

  bool   previousState[MESSAGE_SLOT_COUNT] = {};
  SmsJob job = {};

  for (;;) {
    scanTriggerEdges(previousState);

    if (xQueueReceive(smsQueue, &job, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (!modemReady) {
        Serial.println("[MODEM] Not ready — attempting reinit...");
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