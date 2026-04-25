// #include "RTU.h"
// #include "Shared.h"
// #include <ModbusRTU.h>

// static ModbusRTU mbRTU;
// static const int RXD2 = 9;
// static const int TXD2 = 10;

// void RTU_init() {
//   Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
//   mbRTU.begin(&Serial2);
//   mbRTU.slave(1);

//   for (uint16_t i = 0; i < HOLDING_REGISTER_COUNT; ++i) mbRTU.addHreg(i, 0);
//   for (uint16_t i = 0; i < INPUT_REGISTER_COUNT;   ++i) mbRTU.addIreg(i, 0);
// }

// void RTU_process() {
//   mbRTU.task();
// }

// // ---------------------------------------------------------------------------
// // syncFrom: read what the RTU master wrote into the Modbus holding registers
// // and push it into shared triggerRegs.
// //
// // Rule: shared memory is the single source of truth.
// //   - If the RTU master wrote 1 → set shared to 1.
// //   - If the RTU master wrote 0 → set shared to 0.
// // Both RTU and TCP do this independently. Because both run on core 1 with
// // the stateMutex guarding every write, the last writer wins — which is
// // exactly the desired OR / pass-through behaviour. The Modem task on core 0
// // detects the rising edge (0→1) from the shared copy and never misses it
// // because it never writes back to trigger registers itself.
// // ---------------------------------------------------------------------------
// void RTU_syncFrom() {
//   for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
//     uint16_t rtuVal = mbRTU.Hreg(TRIGGER_REGISTER_START + i);
//     Shared_writeTriggerRegister(i, rtuVal);
//   }
// }

// // ---------------------------------------------------------------------------
// // syncTo: push shared state back into the RTU server registers so a connected
// // master can read the current trigger values, result codes, and input regs.
// // We do NOT write trigger regs back here — that is RTU_syncFrom's job and
// // doing it in syncTo would race with the next syncFrom call.
// // ---------------------------------------------------------------------------
// void RTU_syncTo() {
//   SystemSnapshot snapshot = Shared_getSnapshot();

//   for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
//     // Mirror trigger state so the master can read back what it wrote
//     mbRTU.Hreg(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
//     // Result registers (status codes, may be negative — encode as uint16)
//     mbRTU.Hreg(RESULT_REGISTER_START  + i, encodeSignedRegister(snapshot.resultRegs[i]));
//   }

//   for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
//     mbRTU.Ireg(i, encodeSignedRegister(snapshot.inputRegs[i]));
//   }
// }

// void RTU_taskLoop(void *pvParameters) {
//   (void)pvParameters;

//   for (;;) {
//     RTU_process();   // Handle incoming Modbus RTU frames
//     RTU_syncFrom();  // Pull master-written values into shared memory
//     RTU_syncTo();    // Push shared state back into RTU registers for readback
//     vTaskDelay(pdMS_TO_TICKS(5));
//   }
// }

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
// syncFrom: read what the RTU master wrote into the Modbus holding registers
// and push it into shared triggerRegs.
//
// Rule: shared memory is the single source of truth.
//   - If the RTU master wrote 1 → set shared to 1.
//   - If the RTU master wrote 0 → set shared to 0.
// Both RTU and TCP do this independently. Because both run on core 1 with
// the stateMutex guarding every write, the last writer wins — which is
// exactly the desired OR / pass-through behaviour. The Modem task on core 0
// detects the rising edge (0→1) from the shared copy and never misses it
// because it never writes back to trigger registers itself.
// ---------------------------------------------------------------------------
void RTU_syncFrom() {
  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    uint16_t rtuVal = mbRTU.Hreg(TRIGGER_REGISTER_START + i);
    uint16_t lastSeen = 0;
    Shared_getRTULastSeenTrigger(i, lastSeen);
    
    if (rtuVal != lastSeen) {
      // RTU master actually changed this register — push the change to shared
      Shared_writeTriggerRegister(i, rtuVal);
      Shared_setRTULastSeenTrigger(i, rtuVal);
    }
    // If unchanged, leave shared alone — TCP may have written a newer value
  }
}

// ---------------------------------------------------------------------------
// syncTo: push shared state back into RTU server registers for readback.
// After mirroring trigger registers to both RTU and TCP servers, call
// Shared_updateLastSeenTriggers() so syncFrom() knows not to re-trigger
// on the values we just mirrored.
// ---------------------------------------------------------------------------
void RTU_syncTo() {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    // Mirror trigger state so both RTU and TCP masters can read what's in shared
    mbRTU.Hreg(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
    // Result registers (status codes, may be negative — encode as uint16)
    mbRTU.Hreg(RESULT_REGISTER_START + i, encodeSignedRegister(snapshot.resultRegs[i]));
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    mbRTU.Ireg(i, encodeSignedRegister(snapshot.inputRegs[i]));
  }
  
  // Update lastSeen tracking to prevent re-triggering on mirror writes
  Shared_updateLastSeenTriggers();
}

void RTU_taskLoop(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    RTU_process();   // Handle incoming Modbus RTU frames
    RTU_syncFrom();  // Pull master-written values into shared memory
    RTU_syncTo();    // Push shared state back into RTU registers for readback
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}