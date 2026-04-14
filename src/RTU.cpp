#include "RTU.h"
#include "Shared.h"
#include <ModbusRTU.h>

static ModbusRTU mbRTU;
static const int RXD2 = 9;  // SD2
static const int TXD2 = 10; // SD3

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

  if (newLed != prevCoilLed_RTU) {
    coilLed = newLed;
    prevCoilLed_RTU = newLed;
    srcLed = SRC_RTU;
    Serial.printf("[RTU] LED -> %s\n", coilLed ? "ON" : "OFF");
  }
  if (newPump != prevCoilPump_RTU) {
    coilPump = newPump;
    prevCoilPump_RTU = newPump;
    srcPump = SRC_RTU;
    Serial.printf("[RTU] Pump -> %s\n", coilPump ? "ON" : "OFF");
  }
  if (newLed2 != prevCoilLed2_RTU) {
    coilLed2 = newLed2;
    prevCoilLed2_RTU = newLed2;
    srcLed2 = SRC_RTU;
    Serial.printf("[RTU] LED2 -> %s\n", coilLed2 ? "ON" : "OFF");
  }
  if (newTemp != prevSetTemp_RTU) {
    setTemp = newTemp;
    prevSetTemp_RTU = newTemp;
    srcTemp = SRC_RTU;
    Serial.printf("[RTU] SetTemp -> %d\n", setTemp);
  }
  if (newSpeed != prevSetSpeed_RTU) {
    setSpeed = newSpeed;
    prevSetSpeed_RTU = newSpeed;
    srcSpeed = SRC_RTU;
    Serial.printf("[RTU] SetSpeed -> %d\n", setSpeed);
  }
}

void RTU_syncTo() {
  mbRTU.Coil(0, coilLed);
  mbRTU.Coil(1, coilPump);
  mbRTU.Coil(2, coilLed2);
  mbRTU.Hreg(0, setTemp);
  mbRTU.Hreg(1, setSpeed);
  mbRTU.Ireg(0, actualTemp);
  mbRTU.Ireg(1, voltage);
  mbRTU.Ireg(2, counterVal);
}
