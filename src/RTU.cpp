#include "RTU.h"
#include "Shared.h"
#include <ModbusRTU.h>

static ModbusRTU mbRTU;
static const int RXD2 = 9;
static const int TXD2 = 10;

void RTU_init() {
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mbRTU.begin(&Serial2);
  mbRTU.slave(1);

  for (uint16_t i = 0; i < HOLDING_REGISTER_COUNT; ++i) mbRTU.addHreg(i, 0);
  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT;   ++i) mbRTU.addIreg(i, 0);
}

void RTU_process() {
  mbRTU.task();
}

// ---------------------------------------------------------------------------
// syncFrom: read what the RTU master wrote and push changes into shared.
//
// Only writes to shared if the RTU register actually changed from what RTU
// last saw. This prevents RTU from clobbering a value TCP wrote and vice
// versa. Shared memory is the single source of truth.
// ---------------------------------------------------------------------------
void RTU_syncFrom() {
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
void RTU_syncTo() {
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