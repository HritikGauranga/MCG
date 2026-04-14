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

void TCP_init() {
  Serial.println("\n=== Initializing Ethernet (DHCP) ===");

  SPI.begin(18, 19, 23, 5);
  Ethernet.init(5);

  Serial.println("Requesting DHCP...");
  bool dhcpOk = (Ethernet.begin(mac) != 0);
  if (!dhcpOk) {
    Serial.println("[DHCP] Failed — attempting static IP fallback");

    // Static fallback (tune these values to your network)
    IPAddress staticIP(192,168,8,200);
    IPAddress staticGateway(192,168,8,1);
    IPAddress staticSubnet(255,255,255,0);
    IPAddress staticDNS(8,8,8,8);

    // Try to set static IP
    Ethernet.begin(mac, staticIP);
    delay(1000);

    if (Ethernet.localIP() == (uint32_t)0) {
      Serial.println("[TCP] Static IP assignment failed");
    } else {
      Serial.print("[TCP] Static IP set: ");
      Serial.println(Ethernet.localIP());
      // Optionally set gateway/subnet via Ethernet.config if available
      #if defined(ETHERNET_HAVE_CONFIG)
      Ethernet.config(staticIP, staticDNS, staticGateway, staticSubnet);
      #endif
    }
  }

  delay(1000);

  Serial.print("IP:      "); Serial.println(Ethernet.localIP());
  Serial.print("Gateway: "); Serial.println(Ethernet.gatewayIP());
  Serial.print("Subnet:  "); Serial.println(Ethernet.subnetMask());
  Serial.print("Link:    "); Serial.println(Ethernet.linkStatus());

  ethServer.begin();

  if (!modbusTCPServer.begin()) {
    Serial.println("[TCP] Server init failed!");
  }

  modbusTCPServer.configureCoils(0, 3);
  modbusTCPServer.configureHoldingRegisters(0, 2);
  modbusTCPServer.configureInputRegisters(0, 3);

}

void TCP_maintainDHCP() {
  unsigned long now = millis();
  if (now - lastDHCPCheck < DHCP_RENEW_MS) return;
  lastDHCPCheck = now;

  int result = Ethernet.maintain();
  switch (result) {
    case 0: break;
    case 1: Serial.println("[DHCP] Renew failed");  break;
    case 2: Serial.print("[DHCP] Renewed — IP: ");
            Serial.println(Ethernet.localIP());      break;
    case 3: Serial.println("[DHCP] Rebind failed"); break;
    case 4: Serial.print("[DHCP] Rebound — IP: ");
            Serial.println(Ethernet.localIP());      break;
  }
}

void TCP_processNetwork() {
  if (clientActive) {
    if (!activeClient.connected()) {
      activeClient.stop();
      clientActive = false;
      Serial.println("[TCP] Client disconnected");
    } else {
      modbusTCPServer.poll();
    }
  } else {
    EthernetClient newClient = ethServer.available();
    if (newClient) {
      activeClient = newClient;
      modbusTCPServer.accept(activeClient);
      clientActive = true;
      Serial.println("[TCP] Client connected");
    }
  }
}

void TCP_syncFrom() {
  bool     newLed   = modbusTCPServer.coilRead(0);
  bool     newPump  = modbusTCPServer.coilRead(1);
  bool     newLed2  = modbusTCPServer.coilRead(2);
  uint16_t newTemp  = modbusTCPServer.holdingRegisterRead(0);
  uint16_t newSpeed = modbusTCPServer.holdingRegisterRead(1);

  if (newLed != prevCoilLed_TCP) {
    coilLed = newLed;
    prevCoilLed_TCP = newLed;
    srcLed = SRC_TCP;
    Serial.printf("[TCP] LED -> %s\n", coilLed ? "ON" : "OFF");
  }
  if (newPump != prevCoilPump_TCP) {
    coilPump = newPump;
    prevCoilPump_TCP = newPump;
    srcPump = SRC_TCP;
    Serial.printf("[TCP] Pump -> %s\n", coilPump ? "ON" : "OFF");
  }
  if (newLed2 != prevCoilLed2_TCP) {
    coilLed2 = newLed2;
    prevCoilLed2_TCP = newLed2;
    srcLed2 = SRC_TCP;
    Serial.printf("[TCP] LED2 -> %s\n", coilLed2 ? "ON" : "OFF");
  }
  if (newTemp != prevSetTemp_TCP) {
    setTemp = newTemp;
    prevSetTemp_TCP = newTemp;
    srcTemp = SRC_TCP;
    Serial.printf("[TCP] SetTemp -> %d\n", setTemp);
  }
  if (newSpeed != prevSetSpeed_TCP) {
    setSpeed = newSpeed;
    prevSetSpeed_TCP = newSpeed;
    srcSpeed = SRC_TCP;
    Serial.printf("[TCP] SetSpeed -> %d\n", setSpeed);
  }
}

void TCP_syncTo() {
  modbusTCPServer.coilWrite(0, coilLed);
  modbusTCPServer.coilWrite(1, coilPump);
  modbusTCPServer.coilWrite(2, coilLed2);
  modbusTCPServer.holdingRegisterWrite(0, setTemp);
  modbusTCPServer.holdingRegisterWrite(1, setSpeed);
  modbusTCPServer.inputRegisterWrite(0, actualTemp);
  modbusTCPServer.inputRegisterWrite(1, voltage);
  modbusTCPServer.inputRegisterWrite(2, counterVal);
}
