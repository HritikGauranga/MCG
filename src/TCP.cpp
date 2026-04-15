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
static bool prevCoilLedTCP = false;
static bool prevCoilPumpTCP = false;
static bool prevCoilLed2TCP = false;
static uint16_t prevSetTempTCP = 20;
static uint16_t prevSetSpeedTCP = 100;

void TCP_init() {
  Serial.println("\n=== Initializing Ethernet (W5500) ===");

  // Reset W5500 first
  const int W5500_RST = 14;
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);
  delay(100);
  digitalWrite(W5500_RST, HIGH);
  delay(200);  // Wait for device to stabilize
  Serial.println("[TCP] W5500 reset complete");

  SPI.begin(18, 19, 23, 5);
  Ethernet.init(5);

  Serial.println("Requesting DHCP...");
  bool dhcpOk = (Ethernet.begin(mac) != 0);
  
  // Check if DHCP succeeded AND got valid gateway
  if (dhcpOk && Ethernet.gatewayIP() != IPAddress(0,0,0,0)) {
    Serial.println("[DHCP] ✓ DHCP successful with valid gateway");
  } else {
    if (!dhcpOk) {
      Serial.println("[DHCP] Failed — attempting static IP fallback");
    } else {
      Serial.println("[DHCP] Incomplete (no gateway) — attempting static IP fallback");
    }

    // Static fallback (tune these values to your network)
    IPAddress staticIP(192,168,8,200);
    IPAddress staticGateway(192,168,8,1);
    IPAddress staticSubnet(255,255,255,0);
    IPAddress staticDNS(8,8,8,8);

    // Try to set static IP
    Ethernet.begin(mac, staticIP);
    delay(1000);

    if (Ethernet.localIP() == IPAddress(0,0,0,0)) {
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

  if (!Shared_lockState()) {
    return;
  }

  if (newLed != prevCoilLedTCP) {
    coilLed = newLed;
    prevCoilLedTCP = newLed;
    srcLed = SRC_TCP;
    Serial.printf("[TCP] LED -> %s\n", coilLed ? "ON" : "OFF");
  }
  if (newPump != prevCoilPumpTCP) {
    coilPump = newPump;
    prevCoilPumpTCP = newPump;
    srcPump = SRC_TCP;
    Serial.printf("[TCP] Pump -> %s\n", coilPump ? "ON" : "OFF");
  }
  if (newLed2 != prevCoilLed2TCP) {
    coilLed2 = newLed2;
    prevCoilLed2TCP = newLed2;
    srcLed2 = SRC_TCP;
    Serial.printf("[TCP] LED2 -> %s\n", coilLed2 ? "ON" : "OFF");
  }
  if (newTemp != prevSetTempTCP) {
    setTemp = newTemp;
    prevSetTempTCP = newTemp;
    srcTemp = SRC_TCP;
    Serial.printf("[TCP] SetTemp -> %d\n", setTemp);
  }
  if (newSpeed != prevSetSpeedTCP) {
    setSpeed = newSpeed;
    prevSetSpeedTCP = newSpeed;
    srcSpeed = SRC_TCP;
    Serial.printf("[TCP] SetSpeed -> %d\n", setSpeed);
  }

  Shared_unlockState();
}

void TCP_syncTo() {
  SystemState snapshot = Shared_getSnapshot();

  modbusTCPServer.coilWrite(0, snapshot.coilLed);
  modbusTCPServer.coilWrite(1, snapshot.coilPump);
  modbusTCPServer.coilWrite(2, snapshot.coilLed2);
  modbusTCPServer.holdingRegisterWrite(0, snapshot.setTemp);
  modbusTCPServer.holdingRegisterWrite(1, snapshot.setSpeed);
  modbusTCPServer.inputRegisterWrite(0, snapshot.actualTemp);
  modbusTCPServer.inputRegisterWrite(1, snapshot.voltage);
  modbusTCPServer.inputRegisterWrite(2, snapshot.counterVal);
}
