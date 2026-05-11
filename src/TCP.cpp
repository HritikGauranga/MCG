#include "TCP.h"
#include "Shared.h"
#include <SPI.h>
#include <ETH.h>
#include <WiFi.h>
#include <ArduinoModbus.h>

static WiFiServer *ethServer = nullptr;
static ModbusTCPServer modbusTCPServer;
static WiFiClient      activeClient;
static bool            clientActive  = false;

// W5500 SPI pin map (matches existing wiring in this project)
static const int ETH_SPI_SCK  = 18;
static const int ETH_SPI_MISO = 19;
static const int ETH_SPI_MOSI = 23;
static const int ETH_PHY_CS   = 5;
static const int ETH_PHY_IRQ  = -1; // Not connected on current hardware
static const int ETH_PHY_RST  = 14;
static const int ETH_W5500_ADDR = 1;

static bool ethInitialized = false;
static bool isValidIP(const IPAddress &ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

static bool waitForEthIP(unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (ETH.linkUp() && isValidIP(ETH.localIP())) return true;
    delay(100);
  }
  return ETH.linkUp() && isValidIP(ETH.localIP());
}

static bool applyStaticEthConfig(const GatewaySettings &settings, const char *reasonTag) {
  IPAddress ip(settings.staticIp[0], settings.staticIp[1], settings.staticIp[2], settings.staticIp[3]);
  IPAddress gw(settings.gatewayIp[0], settings.gatewayIp[1], settings.gatewayIp[2], settings.gatewayIp[3]);
  IPAddress sn(settings.subnetMask[0], settings.subnetMask[1], settings.subnetMask[2], settings.subnetMask[3]);

  if (!isValidIP(ip)) {
    Serial.print("[ETH] ERROR: invalid static IP for ");
    Serial.println(reasonTag);
    return false;
  }

  if (!ETH.config(ip, gw, sn, gw, gw)) {
    Serial.print("[ETH] ERROR: ETH.config failed for ");
    Serial.println(reasonTag);
    return false;
  }

  if (!waitForEthIP(10000)) {
    Serial.print("[ETH] ERROR: no valid IP after static config for ");
    Serial.println(reasonTag);
    return false;
  }

  return true;
}

void TCP_init() {
  GatewaySettings settings = {};
  Shared_getGatewaySettings(settings);

  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_PHY_CS);

  Serial.println("[ETH] Starting Ethernet...");

  // Use ESP32 lwIP Ethernet driver for W5500 so Web UI and TCP share one stack.
  if (!ETH.begin(ETH_PHY_W5500, ETH_W5500_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI)) {
    Serial.println("[ETH] ERROR: ETH.begin failed");
  } else {
    ethInitialized = true;
  }

  bool networkReady = false;

  if (ethInitialized && settings.useDhcp) {
    Serial.println("[ETH] DHCP mode");
    networkReady = waitForEthIP(8000);

    if (!networkReady) {
      Serial.println("[ETH] DHCP timeout, switching to static IP fallback");
      networkReady = applyStaticEthConfig(settings, "DHCP fallback");
    }
  } else if (ethInitialized) {
    Serial.println("[ETH] Using static IP");
    networkReady = applyStaticEthConfig(settings, "static mode");
  }

  Serial.print("[ETH] IP: ");      Serial.println(ETH.localIP());
  Serial.print("[ETH] Subnet: ");  Serial.println(ETH.subnetMask());
  Serial.print("[ETH] Gateway: "); Serial.println(ETH.gatewayIP());
  Serial.print("[ETH] Modbus TCP Port: ");
  Serial.println(settings.tcpPort);

  if (!ethInitialized || !networkReady) {
    Serial.println("[ETH] ERROR: Ethernet not ready, Modbus TCP server not started");
    ethServer = nullptr;
    return;
  }

  static WiFiServer serverInstance(settings.tcpPort);
  ethServer = &serverInstance;
  ethServer->begin();
  if (!modbusTCPServer.begin()) {
    Serial.println("[ETH] ERROR: ModbusTCPServer.begin() failed");
    ethServer = nullptr;
    return;
  }
  modbusTCPServer.configureHoldingRegisters(0, HOLDING_REGISTER_COUNT);
  modbusTCPServer.configureInputRegisters(0, INPUT_REGISTER_COUNT);

  Serial.println("[ETH] Modbus TCP server ready");
}

void TCP_maintainDHCP() {
  // DHCP renew is handled by lwIP ETH driver internally.
}

void TCP_processNetwork() {
  if (ethServer == nullptr) return;

  if (clientActive) {
    if (!activeClient.connected()) {
      activeClient.stop();
      clientActive = false;
    } else {
      modbusTCPServer.poll();
    }
    return;
  }

  WiFiClient newClient = ethServer->available();
  if (newClient) {
    activeClient = newClient;
    modbusTCPServer.accept(activeClient);
    clientActive = true;
  }
}

// ---------------------------------------------------------------------------
// syncFrom: read what the TCP master wrote and push changes into shared.
//
// Only writes to shared if the TCP register actually changed from what TCP
// last saw. This prevents TCP from clobbering a value RTU wrote and vice
// versa. Shared memory is the single source of truth.
// ---------------------------------------------------------------------------
void TCP_syncFrom() {
  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    uint16_t tcpVal  = (uint16_t)modbusTCPServer.holdingRegisterRead(TRIGGER_REGISTER_START + i);
    uint16_t lastSeen = 0;
    Shared_getTCPLastSeenTrigger(i, lastSeen);

    if (tcpVal != lastSeen) {
      Shared_writeTriggerRegister(i, tcpVal);
      Shared_setTCPLastSeenTrigger(i, tcpVal);
    }
  }
}

// ---------------------------------------------------------------------------
// syncTo: push shared state back into TCP server registers for readback.
// Only updates TCP's own lastSeen — never touches RTU's lastSeen.
// This prevents the clobber race where RTU_syncFrom mistakes TCP's mirror
// write as a new RTU master write.
// ---------------------------------------------------------------------------
void TCP_syncTo() {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    modbusTCPServer.holdingRegisterWrite(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
    modbusTCPServer.holdingRegisterWrite(RESULT_REGISTER_START  + i, encodeSignedRegister(snapshot.resultRegs[i]));
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    modbusTCPServer.inputRegisterWrite(i, encodeSignedRegister(snapshot.inputRegs[i]));
  }

  // Only update TCP's lastSeen — RTU manages its own independently
  Shared_updateTCPLastSeenTriggers();
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
