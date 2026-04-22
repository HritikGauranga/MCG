#include "TCP.h"
#include "Shared.h"
#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoModbus.h>

static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};
static EthernetServer ethServer(502);
static ModbusTCPServer modbusTCPServer;
static EthernetClient activeClient;
static bool clientActive = false;
static unsigned long lastDHCPCheck = 0;

void TCP_init() {
  const int W5500_RST = 14;
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);
  delay(100);
  digitalWrite(W5500_RST, HIGH);
  delay(200);

  SPI.begin(18, 19, 23, 5);
  Ethernet.init(5);

  Serial.println("[ETH] Starting Ethernet...");
  
  bool dhcpOk = (Ethernet.begin(mac) != 0);
  if (!dhcpOk || Ethernet.gatewayIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("[ETH] DHCP failed, using static IP");
    Ethernet.begin(mac, IPAddress(192, 168, 8, 200));
  } else {
    Serial.println("[ETH] DHCP OK");
  }

  Serial.print("[ETH] IP: ");
  Serial.println(Ethernet.localIP());
  Serial.print("[ETH] Subnet: ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("[ETH] Gateway: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("[ETH] Modbus TCP Port: 502");

  ethServer.begin();
  modbusTCPServer.begin();

  modbusTCPServer.configureHoldingRegisters(0, HOLDING_REGISTER_COUNT);
  modbusTCPServer.configureInputRegisters(0, INPUT_REGISTER_COUNT);
  
  Serial.println("[ETH] Modbus TCP server ready");
}

void TCP_maintainDHCP() {
  unsigned long now = millis();
  if (now - lastDHCPCheck < DHCP_RENEW_MS) {
    return;
  }

  lastDHCPCheck = now;
  Ethernet.maintain();
}

void TCP_processNetwork() {
  if (clientActive) {
    if (!activeClient.connected()) {
      activeClient.stop();
      clientActive = false;
    } else {
      modbusTCPServer.poll();
    }
    return;
  }

  EthernetClient newClient = ethServer.available();
  if (newClient) {
    activeClient = newClient;
    modbusTCPServer.accept(activeClient);
    clientActive = true;
  }
}

void TCP_syncFrom() {
  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    Shared_writeTriggerRegister(i, modbusTCPServer.holdingRegisterRead(TRIGGER_REGISTER_START + i));
  }
}

void TCP_syncTo() {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    modbusTCPServer.holdingRegisterWrite(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
    modbusTCPServer.holdingRegisterWrite(RESULT_REGISTER_START + i, encodeSignedRegister(snapshot.resultRegs[i]));
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    modbusTCPServer.inputRegisterWrite(i, encodeSignedRegister(snapshot.inputRegs[i]));
  }
}

void TCP_taskLoop(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    TCP_processNetwork();
    TCP_syncFrom();
    TCP_syncTo();
    TCP_maintainDHCP();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
