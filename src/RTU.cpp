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

  for (uint16_t i = 0; i < HOLDING_REGISTER_COUNT; ++i) {
    mbRTU.addHreg(i, 0);
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    mbRTU.addIreg(i, 0);
  }
}

void RTU_process() {
  mbRTU.task();
}

void RTU_syncFrom() {
  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    Shared_writeTriggerRegister(i, mbRTU.Hreg(TRIGGER_REGISTER_START + i));
  }
}

void RTU_syncTo() {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    mbRTU.Hreg(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
    mbRTU.Hreg(RESULT_REGISTER_START + i, encodeSignedRegister(snapshot.resultRegs[i]));
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    mbRTU.Ireg(i, encodeSignedRegister(snapshot.inputRegs[i]));
  }
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
