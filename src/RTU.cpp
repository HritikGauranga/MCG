#include "RTU.h"
#include "Shared.h"
#include <ModbusRTU.h>

static ModbusRTU mbRTU;

// Current development wiring uses an FT232 UART adapter on GPIO 9/10.
// For production RS485 hardware, use safe ESP32 UART pins instead:
// static const int RXD2 = 25;
// static const int TXD2 = 26;
static const int RXD2 = 9;
static const int TXD2 = 10;

void RTU_init() {
  GatewaySettings settings = {};
  Shared_getGatewaySettings(settings);

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

static void RTU_process() {
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
