#include "RTU.h"
#include "Shared.h"
#include <ModbusRTU.h>

static ModbusRTU mbRTU;
static bool rtuAsciiMode = false;
static char asciiLine[530] = {};
static size_t asciiLineLen = 0;

// Current development wiring uses an FT232 UART adapter on GPIO 9/10.
// For production RS485 hardware, use safe ESP32 UART pins instead:
// static const int RXD2 = 25;
// static const int TXD2 = 26;
static const int RXD2 = 9;
static const int TXD2 = 10;

void RTU_init() {
  GatewaySettings settings = {};
  Shared_getGatewaySettings(settings);
  rtuAsciiMode = settings.dataBits == 7;

  uint32_t serialCfg = SERIAL_8N1;

if      (settings.dataBits == 7 && settings.parity == 'N' && settings.stopBits == 1) serialCfg = SERIAL_7N1;
else if (settings.dataBits == 7 && settings.parity == 'N' && settings.stopBits == 2) serialCfg = SERIAL_7N2;

else if (settings.dataBits == 7 && settings.parity == 'E' && settings.stopBits == 1) serialCfg = SERIAL_7E1;
else if (settings.dataBits == 7 && settings.parity == 'E' && settings.stopBits == 2) serialCfg = SERIAL_7E2;

else if (settings.dataBits == 7 && settings.parity == 'O' && settings.stopBits == 1) serialCfg = SERIAL_7O1;
else if (settings.dataBits == 7 && settings.parity == 'O' && settings.stopBits == 2) serialCfg = SERIAL_7O2;

else if (settings.dataBits == 8 && settings.parity == 'N' && settings.stopBits == 1) serialCfg = SERIAL_8N1;
else if (settings.dataBits == 8 && settings.parity == 'N' && settings.stopBits == 2) serialCfg = SERIAL_8N2;

else if (settings.dataBits == 8 && settings.parity == 'E' && settings.stopBits == 1) serialCfg = SERIAL_8E1;
else if (settings.dataBits == 8 && settings.parity == 'E' && settings.stopBits == 2) serialCfg = SERIAL_8E2;

else if (settings.dataBits == 8 && settings.parity == 'O' && settings.stopBits == 1) serialCfg = SERIAL_8O1;
else if (settings.dataBits == 8 && settings.parity == 'O' && settings.stopBits == 2) serialCfg = SERIAL_8O2;

Serial2.begin(settings.baudRate, serialCfg, RXD2, TXD2);

  if (rtuAsciiMode) {
    Serial.println("[RTU] Modbus ASCII mode active for 7-bit serial configuration");
    return;
  }

  // uint32_t serialCfg = SERIAL_8N1;
  // if (settings.dataBits == 7 && settings.parity == 'E' && settings.stopBits == 1) serialCfg = SERIAL_7E1;
  // else if (settings.dataBits == 7 && settings.parity == 'O' && settings.stopBits == 1) serialCfg = SERIAL_7O1;
  // else if (settings.dataBits == 8 && settings.parity == 'E' && settings.stopBits == 1) serialCfg = SERIAL_8E1;
  // else if (settings.dataBits == 8 && settings.parity == 'O' && settings.stopBits == 1) serialCfg = SERIAL_8O1;
  // else if (settings.dataBits == 8 && settings.parity == 'N' && settings.stopBits == 2) serialCfg = SERIAL_8N2;

  // Serial2.begin(settings.baudRate, serialCfg, RXD2, TXD2);
  mbRTU.begin(&Serial2);
  mbRTU.slave(settings.slaveId);

  for (uint16_t i = 0; i < HOLDING_REGISTER_COUNT; ++i) mbRTU.addHreg(i, 0);
  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT;   ++i) mbRTU.addIreg(i, 0);
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool decodeAsciiFrame(const char *line, size_t lineLen, uint8_t *frame, size_t frameCapacity, size_t &frameLen) {
  frameLen = 0;
  if (lineLen < 3 || line[0] != ':') return false;

  size_t hexLen = lineLen - 1;
  if ((hexLen & 1) != 0) return false;
  if ((hexLen / 2) > frameCapacity) return false;

  for (size_t i = 1; i < lineLen; i += 2) {
    int hi = hexNibble(line[i]);
    int lo = hexNibble(line[i + 1]);
    if (hi < 0 || lo < 0) return false;
    frame[frameLen++] = (uint8_t)((hi << 4) | lo);
  }

  if (frameLen < 2) return false;

  uint8_t sum = 0;
  for (size_t i = 0; i < frameLen; ++i) sum = (uint8_t)(sum + frame[i]);
  return sum == 0;
}

static uint8_t modbusAsciiLRC(const uint8_t *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) sum = (uint8_t)(sum + data[i]);
  return (uint8_t)(-sum);
}

static void writeHexByte(uint8_t value) {
  static const char hex[] = "0123456789ABCDEF";
  Serial2.write(hex[(value >> 4) & 0x0F]);
  Serial2.write(hex[value & 0x0F]);
}

static void sendAsciiResponse(const uint8_t *pdu, size_t len) {
  Serial2.write(':');
  for (size_t i = 0; i < len; ++i) writeHexByte(pdu[i]);
  writeHexByte(modbusAsciiLRC(pdu
    
    , len));
  Serial2.write('\r');
  Serial2.write('\n');
}

static void sendAsciiException(uint8_t slaveId, uint8_t functionCode, uint8_t exceptionCode) {
  uint8_t response[] = {slaveId, (uint8_t)(functionCode | 0x80), exceptionCode};
  sendAsciiResponse(response, sizeof(response));
}

static bool holdingRegisterValue(uint16_t address, uint16_t &value) {
  SystemSnapshot snapshot = Shared_getSnapshot();
  if (address < MESSAGE_SLOT_COUNT) {
    value = snapshot.triggerRegs[address];
    return true;
  }
  if (address >= RESULT_REGISTER_START && address < RESULT_REGISTER_START + MESSAGE_SLOT_COUNT) {
    value = encodeSignedRegister(snapshot.resultRegs[address - RESULT_REGISTER_START]);
    return true;
  }
  return false;
}

static bool inputRegisterValue(uint16_t address, uint16_t &value) {
  if (address >= INPUT_REGISTER_COUNT) return false;
  SystemSnapshot snapshot = Shared_getSnapshot();
  value = encodeSignedRegister(snapshot.inputRegs[address]);
  return true;
}

static bool writeHoldingRegister(uint16_t address, uint16_t value) {
  if (address >= MESSAGE_SLOT_COUNT) return false;
  if (!Shared_writeTriggerRegister(address, value)) return false;
  Shared_setRTULastSeenTrigger(address, value);
  return true;
}

static void handleAsciiRequest(const uint8_t *frame, size_t len) {
  GatewaySettings settings = {};
  Shared_getGatewaySettings(settings);

  uint8_t slaveId = frame[0];
  if (slaveId != settings.slaveId && slaveId != 0) return;
  if (len < 3) return;

  uint8_t functionCode = frame[1];
  bool broadcast = slaveId == 0;

  if (functionCode == 0x03 || functionCode == 0x04) {
    if (len != 6) {
      if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x03);
      return;
    }

    uint16_t start = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t count = ((uint16_t)frame[4] << 8) | frame[5];
    if (count == 0 || count > 125) {
      if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x03);
      return;
    }

    uint8_t response[256] = {};
    response[0] = settings.slaveId;
    response[1] = functionCode;
    response[2] = (uint8_t)(count * 2);

    for (uint16_t i = 0; i < count; ++i) {
      uint16_t value = 0;
      bool ok = functionCode == 0x03
        ? holdingRegisterValue(start + i, value)
        : inputRegisterValue(start + i, value);
      if (!ok) {
        if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x02);
        return;
      }
      response[3 + i * 2] = (uint8_t)(value >> 8);
      response[4 + i * 2] = (uint8_t)(value & 0xFF);
    }

    if (!broadcast) sendAsciiResponse(response, 3 + count * 2);
    return;
  }

  if (functionCode == 0x06) {
    if (len != 6) {
      if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x03);
      return;
    }

    uint16_t address = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];
    if (!writeHoldingRegister(address, value)) {
      if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x02);
      return;
    }

    if (!broadcast) sendAsciiResponse(frame, len);
    return;
  }

  if (functionCode == 0x10) {
    if (len < 7) {
      if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x03);
      return;
    }

    uint16_t start = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t count = ((uint16_t)frame[4] << 8) | frame[5];
    uint8_t byteCount = frame[6];
    if (count == 0 || count > 123 || byteCount != count * 2 || len != (size_t)(7 + byteCount)) {
      if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x03);
      return;
    }

    for (uint16_t i = 0; i < count; ++i) {
      uint16_t value = ((uint16_t)frame[7 + i * 2] << 8) | frame[8 + i * 2];
      if (!writeHoldingRegister(start + i, value)) {
        if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x02);
        return;
      }
    }

    uint8_t response[] = {settings.slaveId, functionCode, frame[2], frame[3], frame[4], frame[5]};
    if (!broadcast) sendAsciiResponse(response, sizeof(response));
    return;
  }

  if (!broadcast) sendAsciiException(settings.slaveId, functionCode, 0x01);
}

static void ASCII_process() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == ':') {
      asciiLineLen = 0;
      asciiLine[asciiLineLen++] = c;
      continue;
    }

    if (asciiLineLen == 0) continue;

    if (c == '\n') {
      if (asciiLineLen > 0 && asciiLine[asciiLineLen - 1] == '\r') asciiLineLen--;
      uint8_t frame[260] = {};
      size_t frameLen = 0;
      if (decodeAsciiFrame(asciiLine, asciiLineLen, frame, sizeof(frame), frameLen)) {
        handleAsciiRequest(frame, frameLen - 1);
      }
      asciiLineLen = 0;
      continue;
    }

    if (asciiLineLen < sizeof(asciiLine) - 1) {
      asciiLine[asciiLineLen++] = c;
    } else {
      asciiLineLen = 0;
    }
  }
}

static void RTU_process() {
  if (rtuAsciiMode) {
    ASCII_process();
    return;
  }
  mbRTU.task();
}

// ---------------------------------------------------------------------------
// syncFrom: read what the RTU master wrote and push changes into shared.
//
// Only writes to shared if the RTU register actually changed from what RTU
// last saw. This prevents RTU from clobbering a value TCP wrote and vice
// versa. Shared memory is the single source of truth.
// ---------------------------------------------------------------------------
static void RTU_syncFrom() {
  if (rtuAsciiMode) return;
  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    uint16_t rtuVal  = mbRTU.Hreg(TRIGGER_REGISTER_START + i);
    uint16_t lastSeen = 0;  
    Shared_getRTULastSeenTrigger(i, lastSeen);

    if (rtuVal != lastSeen) {
      Shared_writeTriggerRegister(i, rtuVal);
      Shared_setRTULastSeenTrigger(i, rtuVal);
    }
  }
}

// ---------------------------------------------------------------------------
// syncTo: push shared state back into RTU server registers for readback.
// Only updates RTU's own lastSeen — never touches TCP's lastSeen.
// This prevents the clobber race where TCP_syncFrom mistakes RTU's mirror
// write as a new TCP master write.
// ---------------------------------------------------------------------------
static void RTU_syncTo() {
  if (rtuAsciiMode) return;
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    mbRTU.Hreg(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
    mbRTU.Hreg(RESULT_REGISTER_START  + i, encodeSignedRegister(snapshot.resultRegs[i]));
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    mbRTU.Ireg(i, encodeSignedRegister(snapshot.inputRegs[i]));
  }

  // Only update RTU's lastSeen — TCP manages its own independently
  Shared_updateRTULastSeenTriggers();
}

void RTU_taskLoop(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    RTU_process();
    RTU_syncFrom();
    RTU_syncTo();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
