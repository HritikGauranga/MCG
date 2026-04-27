#include "Shared.h"
#include <LittleFS.h>

const int BUTTON_PIN   = 33;
const int MODEM_RX     = 16;
const int MODEM_TX     = 17;
const int MODEM_PWRKEY = 32;

const unsigned long DHCP_RENEW_MS      = 60000;
const unsigned long BUTTON_DEBOUNCE_MS = 100;

SemaphoreHandle_t stateMutex      = nullptr;
SemaphoreHandle_t filesystemMutex = nullptr;

static bool     apModeActive = false;
static uint16_t triggerRegs[MESSAGE_SLOT_COUNT]  = {};
static int16_t  resultRegs[MESSAGE_SLOT_COUNT]   = {};
static int16_t  inputRegs[INPUT_REGISTER_COUNT]  = {
  (int16_t)STATE_READY,
  (int16_t)STATE_UNKNOWN,
  (int16_t)STATE_UNKNOWN,
  (int16_t)STATE_UNKNOWN
};
static MessageConfig messageConfigs[MESSAGE_SLOT_COUNT] = {};
static size_t loadedMessageCount = 0;

// ---------------------------------------------------------------------------
// LastSeen tracking — both arrays are guarded by stateMutex.
// RTU and TCP tasks read/write these via the get/set helpers below.
// Shared_updateLastSeenTriggers() is called from syncTo() after mirroring
// so that neither interface mistakes its own mirror as a new master write.
// ---------------------------------------------------------------------------
static uint16_t rtuLastSeenTriggers[MESSAGE_SLOT_COUNT] = {};
static uint16_t tcpLastSeenTriggers[MESSAGE_SLOT_COUNT] = {};

void Shared_updateRTULastSeenTriggers() {
  if (!Shared_lockState(pdMS_TO_TICKS(50))) return;
  for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    rtuLastSeenTriggers[i] = triggerRegs[i];
  }
  Shared_unlockState();
}

void Shared_updateTCPLastSeenTriggers() {
  if (!Shared_lockState(pdMS_TO_TICKS(50))) return;
  for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    tcpLastSeenTriggers[i] = triggerRegs[i];
  }
  Shared_unlockState();
}

bool Shared_getRTULastSeenTrigger(size_t index, uint16_t &value) {
  if (index >= MESSAGE_SLOT_COUNT) return false;
  if (!Shared_lockState()) return false;
  value = rtuLastSeenTriggers[index];
  Shared_unlockState();
  return true;
}

bool Shared_getTCPLastSeenTrigger(size_t index, uint16_t &value) {
  if (index >= MESSAGE_SLOT_COUNT) return false;
  if (!Shared_lockState()) return false;
  value = tcpLastSeenTriggers[index];
  Shared_unlockState();
  return true;
}

bool Shared_setRTULastSeenTrigger(size_t index, uint16_t value) {
  if (index >= MESSAGE_SLOT_COUNT) return false;
  if (!Shared_lockState()) return false;
  rtuLastSeenTriggers[index] = value;
  Shared_unlockState();
  return true;
}

bool Shared_setTCPLastSeenTrigger(size_t index, uint16_t value) {
  if (index >= MESSAGE_SLOT_COUNT) return false;
  if (!Shared_lockState()) return false;
  tcpLastSeenTriggers[index] = value;
  Shared_unlockState();
  return true;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
String Shared_trimCopy(const String &value) {
  String copy = value;
  copy.trim();
  return copy;
}

// ---------------------------------------------------------------------------
// CSV parser
// ---------------------------------------------------------------------------
static bool parseMessageLine(const String &line, MessageConfig &config) {
  int commas[6] = {-1, -1, -1, -1, -1, -1};
  int found = 0;

  for (int i = 0; i < (int)line.length() && found < 6; ++i) {
    if (line.charAt(i) == ',') commas[found++] = i;
  }

  if (found < 6) return false;

  String msgNoStr = Shared_trimCopy(line.substring(0, commas[0]));
  int msgNo = msgNoStr.toInt();
  if (msgNo < 1 || msgNo > (int)MESSAGE_SLOT_COUNT) return false;

  memset(&config, 0, sizeof(config));
  config.valid = true;
  config.msgNo = (uint8_t)msgNo;

  for (size_t phoneIndex = 0; phoneIndex < PHONE_SLOTS_PER_MESSAGE; ++phoneIndex) {
    int start = commas[phoneIndex] + 1;
    int end   = commas[phoneIndex + 1];
    String number = Shared_trimCopy(line.substring(start, end));
    if (number.length() == 0) continue;
    number.toCharArray(config.phoneNumbers[phoneIndex], PHONE_NUMBER_LENGTH);
    config.phoneCount++;
  }

  String message = Shared_trimCopy(line.substring(commas[5] + 1));
  message.toCharArray(config.text, MESSAGE_TEXT_LENGTH);
  return message.length() > 0;
}

static void clearMessageConfig() {
  memset(messageConfigs, 0, sizeof(messageConfigs));
  loadedMessageCount = 0;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void Shared_init() {
  if (stateMutex == nullptr)      stateMutex      = xSemaphoreCreateMutex();
  if (filesystemMutex == nullptr) filesystemMutex = xSemaphoreCreateMutex();
}

// ---------------------------------------------------------------------------
// Mutex helpers
// ---------------------------------------------------------------------------
bool Shared_lockState(TickType_t timeout) {
  return stateMutex != nullptr && xSemaphoreTake(stateMutex, timeout) == pdTRUE;
}

void Shared_unlockState() {
  if (stateMutex != nullptr) xSemaphoreGive(stateMutex);
}

bool Shared_lockFileSystem(TickType_t timeout) {
  return filesystemMutex != nullptr && xSemaphoreTake(filesystemMutex, timeout) == pdTRUE;
}

void Shared_unlockFileSystem() {
  if (filesystemMutex != nullptr) xSemaphoreGive(filesystemMutex);
}

// ---------------------------------------------------------------------------
// Config load
// ---------------------------------------------------------------------------
bool Shared_loadMessageConfig() {
  static MessageConfig parsedConfigs[MESSAGE_SLOT_COUNT];
  size_t parsedCount = 0;
  memset(parsedConfigs, 0, sizeof(parsedConfigs));

  if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) return false;

  File f = LittleFS.open("/MBmapconf.csv", "r");
  if (!f) {
    Shared_unlockFileSystem();
    if (Shared_lockState(pdMS_TO_TICKS(2000))) {
      clearMessageConfig();
      Shared_unlockState();
    }
    return false;
  }

  if (f.available()) f.readStringUntil('\n'); // skip header

  while (f.available()) {
    String line = Shared_trimCopy(f.readStringUntil('\n'));
    if (line.length() == 0) continue;
    MessageConfig config = {};
    if (!parseMessageLine(line, config)) continue;
    size_t slot = (size_t)(config.msgNo - 1);
    parsedConfigs[slot] = config;
    parsedCount++;
  }

  f.close();
  Shared_unlockFileSystem();

  if (!Shared_lockState(pdMS_TO_TICKS(2000))) return false;
  memset(messageConfigs, 0, sizeof(messageConfigs));
  memcpy(messageConfigs, parsedConfigs, sizeof(parsedConfigs));
  loadedMessageCount = parsedCount;
  Shared_unlockState();
  return true;
}

size_t Shared_getLoadedMessageCount() {
  size_t count = 0;
  if (Shared_lockState(pdMS_TO_TICKS(100))) {
    count = loadedMessageCount;
    Shared_unlockState();
  }
  return count;
}

bool Shared_getMessageConfig(size_t index, MessageConfig &config) {
  if (index >= MESSAGE_SLOT_COUNT) return false;
  if (!Shared_lockState()) return false;
  config = messageConfigs[index];
  Shared_unlockState();
  return config.valid;
}

// ---------------------------------------------------------------------------
// Snapshot & register access
// ---------------------------------------------------------------------------
SystemSnapshot Shared_getSnapshot() {
  SystemSnapshot snapshot = {};
  if (!Shared_lockState()) return snapshot;
  snapshot.apModeActive = apModeActive;
  memcpy(snapshot.triggerRegs, triggerRegs, sizeof(triggerRegs));
  memcpy(snapshot.resultRegs,  resultRegs,  sizeof(resultRegs));
  memcpy(snapshot.inputRegs,   inputRegs,   sizeof(inputRegs));
  Shared_unlockState();
  return snapshot;
}

bool Shared_readTriggerRegister(size_t index, uint16_t &value) {
  if (index >= MESSAGE_SLOT_COUNT || !Shared_lockState()) return false;
  value = triggerRegs[index];
  Shared_unlockState();
  return true;
}

bool Shared_writeTriggerRegister(size_t index, uint16_t value) {
  if (index >= MESSAGE_SLOT_COUNT || !Shared_lockState()) return false;
  triggerRegs[index] = value;
  Shared_unlockState();
  return true;
}

bool Shared_writeResultRegister(size_t index, int16_t value) {
  if (index >= MESSAGE_SLOT_COUNT || !Shared_lockState()) return false;
  resultRegs[index] = value;
  Shared_unlockState();
  return true;
}

bool Shared_writeInputRegister(size_t index, int16_t value) {
  if (index >= INPUT_REGISTER_COUNT || !Shared_lockState()) return false;
  inputRegs[index] = value;
  Shared_unlockState();
  return true;
}

bool Shared_isAPModeActive() {
  bool active = false;
  if (Shared_lockState()) {
    active = apModeActive;
    Shared_unlockState();
  }
  return active;
}

void Shared_setAPModeActive(bool active) {
  if (Shared_lockState()) {
    apModeActive = active;
    Shared_unlockState();
  }
}

uint16_t encodeSignedRegister(int16_t value) {
  return static_cast<uint16_t>(value);
}