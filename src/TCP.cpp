#include "TCP.h"
#include "Shared.h"
#include <SPI.h>
#include <ETH.h>
#include <WiFi.h>
#include <ArduinoModbus.h>
#include <esp_netif.h>

static WiFiServer *ethServer = nullptr;
static ModbusTCPServer modbusTCPServer;
static WiFiClient      activeClient;
static bool            clientActive  = false;
static unsigned long   suppressSyncFromUntilMs = 0;
static unsigned long   lastModbusTcpActivityMs = 0;
static unsigned long   lastModbusTcpStatusLogMs = 0;
static bool            modbusTcpActiveLogged = false;

// W5500 SPI pin map (matches existing wiring in this project)
static const int ETH_SPI_SCK  = 18;
static const int ETH_SPI_MISO = 19;
static const int ETH_SPI_MOSI = 23;
static const int ETH_PHY_CS   = 5;
static const int ETH_PHY_IRQ  = -1; // Not connected on current hardware
static const int ETH_PHY_RST  = 14;
static const int ETH_W5500_ADDR = 1;

static bool ethInitialized = false;
static bool dhcpConfigured = false;
static bool runningOnStaticFallback = false;
static bool networkReady = false;
static unsigned long lastDhcpReacquireMs = 0;
static unsigned long lastLinkCheckMs = 0;
static constexpr unsigned long DHCP_REACQUIRE_INTERVAL_MS = 30000;
static constexpr uint8_t DHCP_REACQUIRE_FAILS_BEFORE_REINIT = 3;
static constexpr unsigned long ETH_LINK_CHECK_INTERVAL_MS = 5000; // Check link every 5 seconds
static constexpr unsigned long DHCP_INITIAL_WAIT_MS = 20000;
static constexpr unsigned long DHCP_STARTUP_RETRY_WAIT_MS = 12000;
static constexpr unsigned long DHCP_REACQUIRE_WAIT_MS = 10000;
static constexpr unsigned long DHCP_LINK_RECOVERY_WAIT_MS = 10000;
static constexpr unsigned long DHCP_SUSTAINED_OUTAGE_BEFORE_STATIC_MS = 120000;
static constexpr unsigned long DHCP_INVALID_IP_RETRY_INTERVAL_MS = 15000;
static constexpr uint8_t DHCP_INVALID_IP_FAILS_BEFORE_REINIT = 3;
static constexpr unsigned long DHCP_HOLDOVER_GRACE_MS = 20000;
static bool lastKnownLinkState = false;
static uint8_t dhcpReacquireFailCount = 0;
static bool hadDhcpLeaseSinceBoot = false;
static unsigned long networkDegradedSinceMs = 0;
static unsigned long lastInvalidIpDhcpRetryMs = 0;
static uint8_t invalidIpDhcpFailCount = 0;
static unsigned long dhcpHoldoverGraceUntilMs = 0;
static unsigned long lastTcpWorkMs = 0;
static uint16_t configuredTcpPort = 502;
static bool invalidIpStateLogged = false;
static bool linkDownStateLogged = false;
static unsigned long lastDhcpPromotionDeferredLogMs = 0;
static bool staticFallbackAutoPromotionLogged = false;
static constexpr unsigned long TCP_IDLE_LOOP_MS = 50;
static constexpr unsigned long TCP_ACTIVE_LOOP_MS = 10;
static constexpr unsigned long MODBUS_TCP_STATUS_LOG_MS = 60000;
static constexpr unsigned long MODBUS_TCP_ACTIVE_WINDOW_MS = 60000;
static constexpr unsigned long DHCP_PROMOTION_DEFER_LOG_INTERVAL_MS = 30000;
static constexpr bool AUTO_PROMOTE_STATIC_FALLBACK_TO_DHCP = false;
static bool waitForEthIP(unsigned long timeoutMs);
static const char *currentEthModeLabel();
static bool acquireDhcpLease(unsigned long timeoutMs);

static void resetTcpServerState(const char *reasonTag) {
  if (clientActive) {
    Serial.print("[MODBUS TCP] Client disconnected: remote=");
    Serial.print(activeClient.remoteIP());
    Serial.print(", reason=");
    Serial.println(reasonTag);
    activeClient.stop();
    clientActive = false;
    lastModbusTcpActivityMs = 0;
    modbusTcpActiveLogged = false;
  }
  if (ethServer != nullptr) {
    ethServer->end();
    ethServer = nullptr;
  }
  Serial.print("[ETH] TCP server reset: ");
  Serial.println(reasonTag);
}

static void logRecoveryOutcome(const char *pathTag) {
  if (!Shared_lockSPI(pdMS_TO_TICKS(5))) return;
  Serial.print("[ETH] Recovery(");
  Serial.print(pathTag);
  Serial.print("): mode=");
  Serial.print(currentEthModeLabel());
  Serial.print(", ip=");
  Serial.println(ETH.localIP());
  Shared_unlockSPI();
}

static bool ensureModbusServerStarted() {
  if (ethServer != nullptr) return true;
  if (!networkReady || !lastKnownLinkState) return false;

  static WiFiServer serverInstance(configuredTcpPort);
  ethServer = &serverInstance;
  ethServer->begin();
  if (!modbusTCPServer.begin()) {
    Serial.println("[ETH] ERROR: ModbusTCPServer.begin() failed");
    ethServer = nullptr;
    return false;
  }
  modbusTCPServer.configureHoldingRegisters(0, HOLDING_REGISTER_COUNT);
  modbusTCPServer.configureInputRegisters(0, INPUT_REGISTER_COUNT);
  Serial.println("[ETH] Modbus TCP server ready");
  return true;
}
static bool isDhcpClientStarted() {
  esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  if (ethNetif == nullptr) return false;

  esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
  if (esp_netif_dhcpc_get_status(ethNetif, &status) != ESP_OK) return false;
  return status == ESP_NETIF_DHCP_STARTED;
}

static const char *currentEthModeLabel() {
  if (!dhcpConfigured) return "STATIC";
  if (runningOnStaticFallback) return "STATIC_FALLBACK";
  return isDhcpClientStarted() ? "DHCP_ACTIVE" : "DHCP_REQUESTED_NO_CLIENT";
}

static void logModbusTcpClientConnected(const WiFiClient &client) {
  Serial.print("[MODBUS TCP] Client connected: remote=");
  Serial.print(client.remoteIP());
  Serial.print(", mode=");
  Serial.print(currentEthModeLabel());
  Serial.print(", local=");
  if (Shared_lockSPI(pdMS_TO_TICKS(5))) {
    Serial.print(ETH.localIP());
    Shared_unlockSPI();
  } else {
    Serial.print("unknown");
  }
  Serial.print(":");
  Serial.println(configuredTcpPort);
  Serial.println("[MODBUS TCP] Status: CONNECTED, activity=WAITING_FOR_REQUESTS");
}

static void logModbusTcpStatusIfDue() {
  if (!clientActive) return;

  unsigned long now = millis();
  if (now - lastModbusTcpStatusLogMs < MODBUS_TCP_STATUS_LOG_MS) return;
  lastModbusTcpStatusLogMs = now;

  bool recentlyActive = lastModbusTcpActivityMs != 0 &&
                        (now - lastModbusTcpActivityMs <= MODBUS_TCP_ACTIVE_WINDOW_MS);

  Serial.print("[MODBUS TCP] Status: CONNECTED, activity=");
  Serial.print(recentlyActive ? "ACTIVE" : "NOT_ACTIVE");
  Serial.print(", remote=");
  Serial.print(activeClient.remoteIP());
  Serial.print(", mode=");
  Serial.print(currentEthModeLabel());
  Serial.print(", local=");
  if (Shared_lockSPI(pdMS_TO_TICKS(5))) {
    Serial.print(ETH.localIP());
    Shared_unlockSPI();
  } else {
    Serial.print("unknown");
  }
  Serial.print(":");
  Serial.print(configuredTcpPort);
  if (lastModbusTcpActivityMs != 0) {
    Serial.print(", last_request_ms_ago=");
    Serial.print(now - lastModbusTcpActivityMs);
  } else {
    Serial.print(", last_request_ms_ago=never");
  }
  Serial.println();
}

static bool isValidIP(const IPAddress &ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

static bool isUsableDhcpLease() {
  if (!Shared_lockSPI(pdMS_TO_TICKS(5))) return false;
  bool result = ETH.linkUp()
      && isValidIP(ETH.localIP())
      && isValidIP(ETH.subnetMask())
      && isValidIP(ETH.gatewayIP());
  Shared_unlockSPI();
  return result;
}

static bool enableDhcpMode() {
  IPAddress zero(0, 0, 0, 0);
  return ETH.config(zero, zero, zero, zero, zero);
}

static bool acquireDhcpLease(unsigned long timeoutMs) {
  if (!enableDhcpMode()) return false;
  return waitForEthIP(timeoutMs) && isUsableDhcpLease();
}

static bool reinitializeEthStackForDhcp() {
  // NOTE:
  // Hard ETH teardown/restart (ETH.end + ETH.begin) can panic on some builds
  // while the lwIP/web stack is active on the other core. Keep recovery
  // non-destructive: re-request DHCP without bringing the interface down.
  Serial.println("[ETH] DHCP recovery: safe DHCP re-acquire (no ETH.end)");
  resetTcpServerState("eth-reinit");

  if (!enableDhcpMode()) {
    Serial.println("[ETH] DHCP recovery: unable to enable DHCP");
    return false;
  }

  if (!waitForEthIP(7000) || !isUsableDhcpLease()) {
    Serial.println("[ETH] DHCP recovery: DHCP still unavailable after safe retry");
    return false;
  }

  Serial.println("[ETH] DHCP recovery: success after safe retry");
  return true;
}

static bool waitForEthIP(unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (!Shared_lockSPI(pdMS_TO_TICKS(2))) {
      delay(50);
      continue;
    }
    bool hasLink = ETH.linkUp() && isValidIP(ETH.localIP());
    Shared_unlockSPI();
    if (hasLink) return true;
    delay(100);
  }
  
  if (!Shared_lockSPI(pdMS_TO_TICKS(2))) return false;
  bool result = ETH.linkUp() && isValidIP(ETH.localIP());
  Shared_unlockSPI();
  return result;
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
  configuredTcpPort = settings.tcpPort;
  dhcpConfigured = settings.useDhcp;
  runningOnStaticFallback = false;
  networkReady = false;
  lastKnownLinkState = false;

  // Harden W5500 bring-up: ensure reset and CS states are clean before ETH.begin().
  pinMode(ETH_PHY_CS, OUTPUT);
  digitalWrite(ETH_PHY_CS, HIGH);
  pinMode(ETH_PHY_RST, OUTPUT);
  digitalWrite(ETH_PHY_RST, LOW);
  delay(50);
  digitalWrite(ETH_PHY_RST, HIGH);
  delay(250);

  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_PHY_CS);
  SPI.setFrequency(8000000); // Safer than high default clocks on long/noisy wiring.

  Serial.println("[ETH] Starting Ethernet...");

  // Use ESP32 lwIP Ethernet driver for W5500 so Web UI and TCP share one stack.
  if (!ETH.begin(ETH_PHY_W5500, ETH_W5500_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI)) {
    Serial.println("[ETH] ERROR: ETH.begin failed");
  } else {
    ethInitialized = true;
  }

  networkReady = false;

  if (ethInitialized && settings.useDhcp) {
    Serial.println("[ETH] DHCP mode");
    // Request DHCP explicitly and wait for a clean lease.
    networkReady = acquireDhcpLease(DHCP_INITIAL_WAIT_MS);

    // Some routers/switches respond slowly right after boot/link-up.
    if (!networkReady) {
      Serial.println("[ETH] DHCP startup retry...");
      networkReady = acquireDhcpLease(DHCP_STARTUP_RETRY_WAIT_MS);
    }

    if (!networkReady) {
      if (Shared_lockSPI(pdMS_TO_TICKS(5))) {
        Serial.print("[ETH] DHCP debug: linkUp=");
        Serial.print(ETH.linkUp() ? "1" : "0");
        Serial.print(", dhcpClient=");
        Serial.print(isDhcpClientStarted() ? "STARTED" : "NOT_STARTED");
        Serial.print(", ip=");
        Serial.print(ETH.localIP());
        Serial.print(", gw=");
        Serial.print(ETH.gatewayIP());
        Serial.print(", sn=");
        Serial.println(ETH.subnetMask());
        Shared_unlockSPI();
      }
      Serial.println("[ETH] DHCP timeout, switching to static IP fallback");
      networkReady = applyStaticEthConfig(settings, "DHCP fallback");
      runningOnStaticFallback = networkReady;
    } else {
      hadDhcpLeaseSinceBoot = true;
    }
  } else if (ethInitialized) {
    Serial.println("[ETH] Using static IP");
    networkReady = applyStaticEthConfig(settings, "static mode");
  }
  lastDhcpReacquireMs = millis();

  if (!Shared_lockSPI(pdMS_TO_TICKS(10))) return;
  Serial.print("[ETH] Mode: ");    Serial.println(currentEthModeLabel());
  Serial.print("[ETH] IP: ");      Serial.println(ETH.localIP());
  Serial.print("[ETH] Subnet: ");  Serial.println(ETH.subnetMask());
  Serial.print("[ETH] Gateway: "); Serial.println(ETH.gatewayIP());
  Shared_unlockSPI();
  Serial.print("[ETH] Modbus TCP Port: ");
  Serial.println(settings.tcpPort);

  if (!ethInitialized || !networkReady) {
    Serial.println("[ETH] ERROR: Ethernet not ready, Modbus TCP server not started");
    ethServer = nullptr;
    return;
  }

  if (Shared_lockSPI(pdMS_TO_TICKS(5))) {
    lastKnownLinkState = ETH.linkUp();
    networkReady = networkReady && lastKnownLinkState && isValidIP(ETH.localIP());
    Shared_unlockSPI();
  } else {
    lastKnownLinkState = false;
    networkReady = false;
  }

  ensureModbusServerStarted();
}

static void TCP_maintainDHCP() {
  if (!ethInitialized || !dhcpConfigured) return;
  if (!runningOnStaticFallback) {
    staticFallbackAutoPromotionLogged = false;
    return;
  }
  unsigned long now = millis();
  if (now - lastDhcpReacquireMs < DHCP_REACQUIRE_INTERVAL_MS) return;
  lastDhcpReacquireMs = now;

  // Keep static fallback always-on unless explicitly enabled.
  // DHCP promotion probes can temporarily drop IP to 0.0.0.0 and disrupt
  // HTTP/Modbus availability even when fallback networking is healthy.
  if (!AUTO_PROMOTE_STATIC_FALLBACK_TO_DHCP) {
    if (!staticFallbackAutoPromotionLogged) {
      Serial.println("[ETH] DHCP auto-promotion disabled while on static fallback");
      lastDhcpPromotionDeferredLogMs = now;
      staticFallbackAutoPromotionLogged = true;
    }
    return;
  }

  // Keep static fallback service uninterrupted. Rebinding to DHCP can
  // temporarily drop the active IP (0.0.0.0 window), so defer promotion while
  // the gateway is actively serving traffic or AP mode is in use.
  if (clientActive || Shared_isAPModeActive()) {
    if (now - lastDhcpPromotionDeferredLogMs >= DHCP_PROMOTION_DEFER_LOG_INTERVAL_MS) {
      Serial.println("[ETH] DHCP promotion deferred to keep static fallback service stable");
      lastDhcpPromotionDeferredLogMs = now;
    }
    return;
  }

  // We are currently on static fallback; explicitly request DHCP first.
  if (!enableDhcpMode()) {
    Serial.println("[ETH] DHCP re-acquire failed: unable to enable DHCP mode");
    return;
  }

  if (waitForEthIP(DHCP_REACQUIRE_WAIT_MS) && isUsableDhcpLease()) {
    runningOnStaticFallback = false;
    networkReady = true;
    hadDhcpLeaseSinceBoot = true;
    networkDegradedSinceMs = 0;
    dhcpReacquireFailCount = 0;
    Serial.println("[ETH] DHCP re-acquire success: switched back to DHCP");
    
    if (!Shared_lockSPI(pdMS_TO_TICKS(5))) return;
    Serial.print("[ETH] Mode: ");    Serial.println(currentEthModeLabel());
    Serial.print("[ETH] IP: ");      Serial.println(ETH.localIP());
    Serial.print("[ETH] Subnet: ");  Serial.println(ETH.subnetMask());
    Serial.print("[ETH] Gateway: "); Serial.println(ETH.gatewayIP());
    Shared_unlockSPI();
    return;
  }

  GatewaySettings settings = {};
  dhcpReacquireFailCount++;
  Serial.print("[ETH] DHCP re-acquire attempt failed, count=");
  Serial.println((unsigned int)dhcpReacquireFailCount);

  if (dhcpReacquireFailCount >= DHCP_REACQUIRE_FAILS_BEFORE_REINIT) {
    if (reinitializeEthStackForDhcp()) {
      runningOnStaticFallback = false;
      networkReady = true;
      hadDhcpLeaseSinceBoot = true;
      networkDegradedSinceMs = 0;
      lastKnownLinkState = true;
      dhcpReacquireFailCount = 0;

      if (Shared_lockSPI(pdMS_TO_TICKS(5))) {
        Serial.print("[ETH] Mode: ");    Serial.println(currentEthModeLabel());
        Serial.print("[ETH] IP: ");      Serial.println(ETH.localIP());
        Serial.print("[ETH] Subnet: ");  Serial.println(ETH.subnetMask());
        Serial.print("[ETH] Gateway: "); Serial.println(ETH.gatewayIP());
        Shared_unlockSPI();
      }
      return;
    }
    dhcpReacquireFailCount = 0;
  }

  if (Shared_getGatewaySettings(settings)) {
    // Keep the existing fallback IP if it's still valid. Re-configuring static
    // on every failed DHCP re-acquire can cause avoidable TCP disruptions.
    if (Shared_lockSPI(pdMS_TO_TICKS(5))) {
      bool hasFallbackIp = ETH.linkUp() && isValidIP(ETH.localIP());
      Shared_unlockSPI();
      if (hasFallbackIp) {
        networkReady = true;
        return;
      }
    }
    networkReady = applyStaticEthConfig(settings, "reassert static fallback");
  }
}

// ---------------------------------------------------------------------------
// Link health monitoring — detect when W5500 loses connection and recover
// ---------------------------------------------------------------------------
void TCP_monitorEthernetLink() {
  if (!ethInitialized) return;

  unsigned long now = millis();
  if (now - lastLinkCheckMs < ETH_LINK_CHECK_INTERVAL_MS) return;
  lastLinkCheckMs = now;

  // Protect W5500 SPI access from LittleFS operations
  if (!Shared_lockSPI(pdMS_TO_TICKS(10))) return;
  
  bool linkUp = ETH.linkUp();
  bool hasValidIP = isValidIP(ETH.localIP());
  
  Shared_unlockSPI();

  // Link state changed or IP became invalid
  if (linkUp != lastKnownLinkState || (linkUp && !hasValidIP)) {
    if (!linkUp) {
      if (!linkDownStateLogged) {
        Serial.println("[ETH] WARNING: Ethernet link lost, attempting recovery...");
        linkDownStateLogged = true;
      }
      invalidIpStateLogged = false;
      lastKnownLinkState = false;
      if (networkDegradedSinceMs == 0) networkDegradedSinceMs = now;
      
      // Reset TCP server/client state so recovery can cleanly rebind.
      resetTcpServerState("link-down");
      
      // Attempt to recover the connection
      GatewaySettings settings = {};
      if (Shared_getGatewaySettings(settings)) {
        if (settings.useDhcp) {
          Serial.println("[ETH] Attempting DHCP recovery...");
          enableDhcpMode();
          if (waitForEthIP(DHCP_LINK_RECOVERY_WAIT_MS) && isUsableDhcpLease()) {
            Serial.println("[ETH] DHCP recovery successful");
            runningOnStaticFallback = false;
            networkReady = true;
            hadDhcpLeaseSinceBoot = true;
            networkDegradedSinceMs = 0;
            lastDhcpReacquireMs = now;
            logRecoveryOutcome("link-lost-dhcp");
          } else {
            unsigned long degradedMs = (networkDegradedSinceMs == 0) ? 0 : (now - networkDegradedSinceMs);
            if (!hadDhcpLeaseSinceBoot || degradedMs >= DHCP_SUSTAINED_OUTAGE_BEFORE_STATIC_MS) {
              Serial.println("[ETH] DHCP recovery failed for sustained window, switching to static IP");
              networkReady = applyStaticEthConfig(settings, "link recovery");
              runningOnStaticFallback = networkReady;
              if (networkReady) logRecoveryOutcome("link-lost-static-fallback");
            } else {
              Serial.println("[ETH] DHCP recovery retry window active, holding DHCP mode");
              networkReady = false;
            }
          }
        } else {
          Serial.println("[ETH] Attempting static IP recovery...");
          networkReady = applyStaticEthConfig(settings, "link recovery");
          if (networkReady) logRecoveryOutcome("link-lost-static");
        }
      }
    } else if (linkUp && !hasValidIP) {
      if (networkDegradedSinceMs == 0) networkDegradedSinceMs = now;
      // Hold Modbus service for a short grace window while DHCP re-acquires.
      // This avoids immediate session drops on brief lease blips.
      if (dhcpConfigured && hadDhcpLeaseSinceBoot) {
        if (dhcpHoldoverGraceUntilMs == 0) {
          dhcpHoldoverGraceUntilMs = now + DHCP_HOLDOVER_GRACE_MS;
          Serial.print("[ETH] DHCP holdover grace started (ms): ");
          Serial.println((unsigned long)DHCP_HOLDOVER_GRACE_MS);
        }
        bool graceActive = (long)(dhcpHoldoverGraceUntilMs - now) > 0;
        networkReady = graceActive;
      } else {
        networkReady = false;
      }
      if (!invalidIpStateLogged) {
        Serial.println("[ETH] WARNING: Link is up but IP is invalid, waiting for DHCP recovery");
        invalidIpStateLogged = true;
      }
      linkDownStateLogged = false;

      // Actively re-request DHCP while link is up but lease is invalid.
      if (dhcpConfigured && (now - lastInvalidIpDhcpRetryMs >= DHCP_INVALID_IP_RETRY_INTERVAL_MS)) {
        lastInvalidIpDhcpRetryMs = now;
        Serial.println("[ETH] Attempting DHCP re-acquire for invalid IP state...");
        if (enableDhcpMode() && waitForEthIP(DHCP_REACQUIRE_WAIT_MS) && isUsableDhcpLease()) {
          Serial.println("[ETH] DHCP re-acquire successful from invalid IP state");
          runningOnStaticFallback = false;
          networkReady = true;
          hadDhcpLeaseSinceBoot = true;
          networkDegradedSinceMs = 0;
          lastKnownLinkState = true;
          lastDhcpReacquireMs = now;
          dhcpReacquireFailCount = 0;
          invalidIpDhcpFailCount = 0;
          invalidIpStateLogged = false;
          linkDownStateLogged = false;
          dhcpHoldoverGraceUntilMs = 0;
          logRecoveryOutcome("invalid-ip-dhcp");
        } else {
          invalidIpDhcpFailCount++;
          Serial.print("[ETH] DHCP re-acquire failed from invalid IP state, count=");
          Serial.println((unsigned int)invalidIpDhcpFailCount);

          if (invalidIpDhcpFailCount >= DHCP_INVALID_IP_FAILS_BEFORE_REINIT) {
            Serial.println("[ETH] Invalid-IP DHCP retries exhausted, forcing Ethernet reinit");
            if (reinitializeEthStackForDhcp()) {
              runningOnStaticFallback = false;
              networkReady = true;
              hadDhcpLeaseSinceBoot = true;
              networkDegradedSinceMs = 0;
              lastKnownLinkState = true;
              lastDhcpReacquireMs = now;
              dhcpReacquireFailCount = 0;
              invalidIpDhcpFailCount = 0;
              invalidIpStateLogged = false;
              linkDownStateLogged = false;
              dhcpHoldoverGraceUntilMs = 0;
              logRecoveryOutcome("invalid-ip-reinit-dhcp");
            } else {
              GatewaySettings settings = {};
              if (Shared_getGatewaySettings(settings)) {
                networkReady = applyStaticEthConfig(settings, "invalid-ip fallback");
                runningOnStaticFallback = networkReady;
                if (networkReady) logRecoveryOutcome("invalid-ip-static-fallback");
              } else {
                networkReady = false;
              }
              dhcpHoldoverGraceUntilMs = 0;
              invalidIpDhcpFailCount = 0;
            }
          }
        }
      }
    } else if (linkUp && hasValidIP && !lastKnownLinkState) {
      Serial.println("[ETH] Ethernet link restored");
      linkDownStateLogged = false;
      invalidIpStateLogged = false;
      dhcpHoldoverGraceUntilMs = 0;

      // If DHCP is configured, explicitly re-request it on link restore.
      // Without this, the stack can remain on a previously applied static IP.
      if (dhcpConfigured) {
        if (enableDhcpMode() &&
            waitForEthIP(DHCP_LINK_RECOVERY_WAIT_MS) &&
            isUsableDhcpLease()) {
          runningOnStaticFallback = false;
          networkReady = true;
          hadDhcpLeaseSinceBoot = true;
          networkDegradedSinceMs = 0;
          lastDhcpReacquireMs = now;
          dhcpReacquireFailCount = 0;
          Serial.println("[ETH] DHCP restored after link recovery");
          logRecoveryOutcome("link-restored-dhcp");
        } else {
          GatewaySettings settings = {};
          if (Shared_getGatewaySettings(settings)) {
            networkReady = applyStaticEthConfig(settings, "link restore fallback");
            runningOnStaticFallback = networkReady;
            if (networkReady) logRecoveryOutcome("link-restored-static-fallback");
          } else {
            networkReady = false;
          }
          Serial.println("[ETH] DHCP not available after link recovery, keeping static fallback");
        }
      } else {
        networkReady = true;
        networkDegradedSinceMs = 0;
      }

      if (!Shared_lockSPI(pdMS_TO_TICKS(5))) return;
      Serial.print("[ETH] Mode: ");    Serial.println(currentEthModeLabel());
      Serial.print("[ETH] IP: ");      Serial.println(ETH.localIP());
      Serial.print("[ETH] Subnet: ");  Serial.println(ETH.subnetMask());
      Serial.print("[ETH] Gateway: "); Serial.println(ETH.gatewayIP());
      Shared_unlockSPI();

      lastKnownLinkState = true;
    }
  }
}

static void TCP_processNetwork() {
  if (ethServer == nullptr || !networkReady || !lastKnownLinkState) return;

  if (clientActive) {
    if (!activeClient.connected()) {
      Serial.print("[MODBUS TCP] Client disconnected: remote=");
      Serial.println(activeClient.remoteIP());
      activeClient.stop();
      clientActive = false;
      lastModbusTcpActivityMs = 0;
      modbusTcpActiveLogged = false;
    } else {
      int handled = modbusTCPServer.poll();
      if (handled > 0) {
        lastModbusTcpActivityMs = millis();
        if (!modbusTcpActiveLogged) {
          Serial.println("[MODBUS TCP] Status: ACTIVE (request received and reply sent)");
          modbusTcpActiveLogged = true;
        }
      }
      logModbusTcpStatusIfDue();
    }
    return;
  }

  WiFiClient newClient = ethServer->accept();
  if (newClient) {
    activeClient = newClient;
    modbusTCPServer.accept(activeClient);
    // New client sessions can momentarily expose default register values
    // before our mirror is pushed. Hold syncFrom briefly to avoid writing
    // transient zeroes back into shared triggers.
    Shared_updateTCPLastSeenTriggers();
    suppressSyncFromUntilMs = millis() + 200;
    clientActive = true;
    lastModbusTcpActivityMs = 0;
    lastModbusTcpStatusLogMs = 0;
    modbusTcpActiveLogged = false;
    logModbusTcpClientConnected(activeClient);
  }
}

// ---------------------------------------------------------------------------
// syncFrom: read what the TCP master wrote and push changes into shared.
//
// Only writes to shared if the TCP register actually changed from what TCP
// last saw. This prevents TCP from clobbering a value RTU wrote and vice
// versa. Shared memory is the single source of truth.
// ---------------------------------------------------------------------------
static void TCP_syncFrom() {
  if (!networkReady || !lastKnownLinkState || !clientActive) return;
  if ((long)(millis() - suppressSyncFromUntilMs) < 0) return;
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
static void TCP_syncTo() {
  if (!networkReady || !lastKnownLinkState) return;
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
    unsigned long now = millis();
    if (now - lastTcpWorkMs < (clientActive ? TCP_ACTIVE_LOOP_MS : TCP_IDLE_LOOP_MS)) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    lastTcpWorkMs = now;

    TCP_monitorEthernetLink();  // Check link health every 5 seconds
    ensureModbusServerStarted();
    TCP_processNetwork();
    TCP_syncFrom();
    TCP_syncTo();
    TCP_maintainDHCP();
    
    vTaskDelay(pdMS_TO_TICKS(clientActive ? 10 : 25));
  }
}
