// #include "TCP.h"
// #include "Shared.h"
// #include <SPI.h>
// #include <Ethernet.h>
// #include <ArduinoModbus.h>

// static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};
// static EthernetServer   ethServer(502);
// static ModbusTCPServer  modbusTCPServer;
// static EthernetClient   activeClient;
// static bool             clientActive  = false;
// static unsigned long    lastDHCPCheck = 0;

// void TCP_init() {
//   const int W5500_RST = 14;
//   pinMode(W5500_RST, OUTPUT);
//   digitalWrite(W5500_RST, LOW);
//   delay(100);
//   digitalWrite(W5500_RST, HIGH);
//   delay(200);

//   SPI.begin(18, 19, 23, 5);
//   Ethernet.init(5);

//   Serial.println("[ETH] Starting Ethernet...");
//   bool dhcpOk = (Ethernet.begin(mac) != 0);
//   if (!dhcpOk || Ethernet.gatewayIP() == IPAddress(0, 0, 0, 0)) {
//     Serial.println("[ETH] DHCP failed, using static IP");
//     Ethernet.begin(mac, IPAddress(192, 168, 8, 200));
//   } else {
//     Serial.println("[ETH] DHCP OK");
//   }

//   Serial.print("[ETH] IP: ");      Serial.println(Ethernet.localIP());
//   Serial.print("[ETH] Subnet: ");  Serial.println(Ethernet.subnetMask());
//   Serial.print("[ETH] Gateway: "); Serial.println(Ethernet.gatewayIP());
//   Serial.println("[ETH] Modbus TCP Port: 502");

//   ethServer.begin();
//   modbusTCPServer.begin();
//   modbusTCPServer.configureHoldingRegisters(0, HOLDING_REGISTER_COUNT);
//   modbusTCPServer.configureInputRegisters(0, INPUT_REGISTER_COUNT);

//   Serial.println("[ETH] Modbus TCP server ready");
// }

// void TCP_maintainDHCP() {
//   unsigned long now = millis();
//   if (now - lastDHCPCheck < DHCP_RENEW_MS) return;
//   lastDHCPCheck = now;
//   Ethernet.maintain();
// }

// void TCP_processNetwork() {
//   if (clientActive) {
//     if (!activeClient.connected()) {
//       activeClient.stop();
//       clientActive = false;
//     } else {
//       modbusTCPServer.poll();
//     }
//     return;
//   }

//   EthernetClient newClient = ethServer.available();
//   if (newClient) {
//     activeClient = newClient;
//     modbusTCPServer.accept(activeClient);
//     clientActive = true;
//   }
// }

// // ---------------------------------------------------------------------------
// // syncFrom: read what the TCP master wrote into the Modbus holding registers
// // and push it into shared triggerRegs.
// //
// // Same rule as RTU: shared memory is the single source of truth.
// // Both TCP and RTU write to the same shared array — stateMutex ensures
// // no torn writes. The Modem task reads from shared and detects rising edges.
// // ---------------------------------------------------------------------------
// void TCP_syncFrom() {
//   for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
//     uint16_t tcpVal = (uint16_t)modbusTCPServer.holdingRegisterRead(TRIGGER_REGISTER_START + i);
//     Shared_writeTriggerRegister(i, tcpVal);
//   }
// }

// // ---------------------------------------------------------------------------
// // syncTo: push shared state back into TCP server registers for readback.
// // ---------------------------------------------------------------------------
// void TCP_syncTo() {
//   SystemSnapshot snapshot = Shared_getSnapshot();

//   for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
//     modbusTCPServer.holdingRegisterWrite(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
//     modbusTCPServer.holdingRegisterWrite(RESULT_REGISTER_START  + i, encodeSignedRegister(snapshot.resultRegs[i]));
//   }

//   for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
//     modbusTCPServer.inputRegisterWrite(i, encodeSignedRegister(snapshot.inputRegs[i]));
//   }
// }

// void TCP_taskLoop(void *pvParameters) {
//   (void)pvParameters;

//   for (;;) {
//     TCP_processNetwork();  // Accept client, poll Modbus frames
//     TCP_syncFrom();        // Pull master-written values into shared memory
//     TCP_syncTo();          // Push shared state back for readback
//     TCP_maintainDHCP();
//     vTaskDelay(pdMS_TO_TICKS(5));
//   }
// }

#include "TCP.h"
#include "Shared.h"
#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoModbus.h>

static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};
static EthernetServer   ethServer(502);
static ModbusTCPServer  modbusTCPServer;
static EthernetClient   activeClient;
static bool             clientActive  = false;
static unsigned long    lastDHCPCheck = 0;

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

  Serial.print("[ETH] IP: ");      Serial.println(Ethernet.localIP());
  Serial.print("[ETH] Subnet: ");  Serial.println(Ethernet.subnetMask());
  Serial.print("[ETH] Gateway: "); Serial.println(Ethernet.gatewayIP());
  Serial.println("[ETH] Modbus TCP Port: 502");

  ethServer.begin();
  modbusTCPServer.begin();
  modbusTCPServer.configureHoldingRegisters(0, HOLDING_REGISTER_COUNT);
  modbusTCPServer.configureInputRegisters(0, INPUT_REGISTER_COUNT);

  Serial.println("[ETH] Modbus TCP server ready");
}

void TCP_maintainDHCP() {
  unsigned long now = millis();
  if (now - lastDHCPCheck < DHCP_RENEW_MS) return;
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

// ---------------------------------------------------------------------------
// syncFrom: read what the TCP master wrote into the Modbus holding registers
// and push it into shared triggerRegs.
//
// Same rule as RTU: shared memory is the single source of truth.
// Both TCP and RTU write to the same shared array — stateMutex ensures
// no torn writes. The Modem task reads from shared and detects rising edges.
// ---------------------------------------------------------------------------
// Track the last value TCP saw so we only write to shared on an actual change.
static uint16_t tcpLastSeen[MESSAGE_SLOT_COUNT] = {};

void TCP_syncFrom() {
  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    uint16_t tcpVal = (uint16_t)modbusTCPServer.holdingRegisterRead(TRIGGER_REGISTER_START + i);
    if (tcpVal != tcpLastSeen[i]) {
      // TCP master actually changed this register — push the change to shared
      Shared_writeTriggerRegister(i, tcpVal);
      tcpLastSeen[i] = tcpVal;
    }
    // If unchanged, leave shared alone — RTU may have written a newer value
  }
}

// ---------------------------------------------------------------------------
// syncTo: push shared state back into TCP server registers for readback.
// Also update tcpLastSeen so syncFrom doesn't treat our own mirror writes
// as new TCP master writes on the next cycle.
// ---------------------------------------------------------------------------
void TCP_syncTo() {
  SystemSnapshot snapshot = Shared_getSnapshot();

  for (uint16_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    modbusTCPServer.holdingRegisterWrite(TRIGGER_REGISTER_START + i, snapshot.triggerRegs[i]);
    modbusTCPServer.holdingRegisterWrite(RESULT_REGISTER_START  + i, encodeSignedRegister(snapshot.resultRegs[i]));
    // Keep tcpLastSeen aligned with what we just wrote so syncFrom
    // does not mistake our own mirror as a new TCP master write
    tcpLastSeen[i] = snapshot.triggerRegs[i];
  }

  for (uint16_t i = 0; i < INPUT_REGISTER_COUNT; ++i) {
    modbusTCPServer.inputRegisterWrite(i, encodeSignedRegister(snapshot.inputRegs[i]));
  }
}

void TCP_taskLoop(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    TCP_processNetwork();  // Accept client, poll Modbus frames
    TCP_syncFrom();        // Pull master-written values into shared memory
    TCP_syncTo();          // Push shared state back for readback
    TCP_maintainDHCP();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}