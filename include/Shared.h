#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

constexpr size_t MESSAGE_SLOT_COUNT = 50;
constexpr size_t PHONE_SLOTS_PER_MESSAGE = 5;
constexpr size_t PHONE_NUMBER_LENGTH = 20;
constexpr size_t MESSAGE_TEXT_LENGTH = 512;
constexpr size_t HOLDING_REGISTER_COUNT = 100;
constexpr size_t INPUT_REGISTER_COUNT = 4;

constexpr size_t TRIGGER_REGISTER_START = 0;
constexpr size_t RESULT_REGISTER_START = 50;
constexpr size_t DEVICE_STATUS_REGISTER = 0;
constexpr size_t MODEM_STATUS_REGISTER = 1;
constexpr size_t SIM_STATUS_REGISTER = 2;
constexpr size_t NETWORK_STATUS_REGISTER = 3;

enum RegisterStatus : int16_t {
  STATUS_IDLE = 0,
  STATUS_ERROR_SEND = -1,
  STATUS_ERROR_SIM = -2,
  STATUS_ERROR_NETWORK = -3,
  STATUS_ERROR_CONFIG = -4,
  STATUS_ERROR_EMPTY = -5,
  STATUS_ERROR_MODEM = -6
};

enum RuntimeState : int16_t {
  STATE_UNKNOWN = 0,
  STATE_READY = 1,
  STATE_BUSY = 2,
  STATE_ERROR = -1
};

struct MessageConfig {
  bool valid;
  uint8_t msgNo;
  uint8_t phoneCount;
  char phoneNumbers[PHONE_SLOTS_PER_MESSAGE][PHONE_NUMBER_LENGTH];
  char text[MESSAGE_TEXT_LENGTH];
};

struct SystemSnapshot {
  bool apModeActive;
  uint16_t triggerRegs[MESSAGE_SLOT_COUNT];
  int16_t resultRegs[MESSAGE_SLOT_COUNT];
  int16_t inputRegs[INPUT_REGISTER_COUNT];
};

extern const int BUTTON_PIN;
extern const int MODEM_RX;
extern const int MODEM_TX;
extern const int MODEM_PWRKEY;

extern const unsigned long DHCP_RENEW_MS;
extern const unsigned long BUTTON_DEBOUNCE_MS;

extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t filesystemMutex;

void Shared_init();
bool Shared_lockState(TickType_t timeout = pdMS_TO_TICKS(50));
void Shared_unlockState();
bool Shared_lockFileSystem(TickType_t timeout = pdMS_TO_TICKS(500));
void Shared_unlockFileSystem();

bool Shared_loadMessageConfig();
size_t Shared_getLoadedMessageCount();
bool Shared_getMessageConfig(size_t index, MessageConfig &config);

SystemSnapshot Shared_getSnapshot();
bool Shared_readTriggerRegister(size_t index, uint16_t &value);
bool Shared_writeTriggerRegister(size_t index, uint16_t value);
bool Shared_writeResultRegister(size_t index, int16_t value);
bool Shared_writeInputRegister(size_t index, int16_t value);
bool Shared_isAPModeActive();
void Shared_setAPModeActive(bool active);

uint16_t encodeSignedRegister(int16_t value);
