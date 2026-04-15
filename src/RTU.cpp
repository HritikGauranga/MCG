#include "RTU.h"
#include "Shared.h"
#include <ModbusRTU.h>

static ModbusRTU mbRTU;
static const int RXD2 = 9;  // SD2
static const int TXD2 = 10; // SD3
static bool prevCoilLedRTU = false;
static bool prevCoilPumpRTU = false;
static bool prevCoilLed2RTU = false;
static uint16_t prevSetTempRTU = 20;
static uint16_t prevSetSpeedRTU = 100;

void RTU_init() {
  pinMode(LED_PIN,  OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);
  digitalWrite(LED_PIN,  LOW);
  digitalWrite(PUMP_PIN, LOW);

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mbRTU.begin(&Serial2);
  mbRTU.slave(1);

  mbRTU.addCoil(0, coilLed);
  mbRTU.addCoil(1, coilPump);
  mbRTU.addCoil(2, coilLed2);
  mbRTU.addHreg(0, setTemp);
  mbRTU.addHreg(1, setSpeed);
  mbRTU.addIreg(0, actualTemp);
  mbRTU.addIreg(1, voltage);
  mbRTU.addIreg(2, counterVal);
}

void RTU_task() {
  mbRTU.task();
}

void RTU_syncFrom() {
  bool     newLed   = mbRTU.Coil(0);
  bool     newPump  = mbRTU.Coil(1);
  bool     newLed2  = mbRTU.Coil(2);
  uint16_t newTemp  = mbRTU.Hreg(0);
  uint16_t newSpeed = mbRTU.Hreg(1);

  if (!Shared_lockState()) {
    return;
  }

  if (newLed != prevCoilLedRTU) {
    coilLed = newLed;
    prevCoilLedRTU = newLed;
    srcLed = SRC_RTU;
    Serial.printf("[RTU] LED -> %s\n", coilLed ? "ON" : "OFF");
  }
  if (newPump != prevCoilPumpRTU) {
    coilPump = newPump;
    prevCoilPumpRTU = newPump;
    srcPump = SRC_RTU;
    Serial.printf("[RTU] Pump -> %s\n", coilPump ? "ON" : "OFF");
  }
  if (newLed2 != prevCoilLed2RTU) {
    coilLed2 = newLed2;
    prevCoilLed2RTU = newLed2;
    srcLed2 = SRC_RTU;
    Serial.printf("[RTU] LED2 -> %s\n", coilLed2 ? "ON" : "OFF");
  }
  if (newTemp != prevSetTempRTU) {
    setTemp = newTemp;
    prevSetTempRTU = newTemp;
    srcTemp = SRC_RTU;
    Serial.printf("[RTU] SetTemp -> %d\n", setTemp);
  }
  if (newSpeed != prevSetSpeedRTU) {
    setSpeed = newSpeed;
    prevSetSpeedRTU = newSpeed;
    srcSpeed = SRC_RTU;
    Serial.printf("[RTU] SetSpeed -> %d\n", setSpeed);
  }

  Shared_unlockState();
}

void RTU_syncTo() {
  SystemState snapshot = Shared_getSnapshot();

  mbRTU.Coil(0, snapshot.coilLed);
  mbRTU.Coil(1, snapshot.coilPump);
  mbRTU.Coil(2, snapshot.coilLed2);
  mbRTU.Hreg(0, snapshot.setTemp);
  mbRTU.Hreg(1, snapshot.setSpeed);
  mbRTU.Ireg(0, snapshot.actualTemp);
  mbRTU.Ireg(1, snapshot.voltage);
  mbRTU.Ireg(2, snapshot.counterVal);
}
