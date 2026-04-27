#include "AP.h"
#include "Shared.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

static AsyncWebServer server(80);
static File           uploadFile;
static bool           serverStarted    = false;
static bool           serverRoutesSetup = false;

void ensureMBMapConfigFile() {
  if (!Shared_lockFileSystem()) return;

  if (!LittleFS.exists("/MBmapconf.csv")) {
    File f = LittleFS.open("/MBmapconf.csv", "w");
    if (f) {
      f.println("Msg.No., Phone number1, Phone number2, Phone number3, Phone number4, Phone number5, Text message");
      f.println("1, 8149979689, 8655138978, 9030123253, 51856452185, 7111111111, ALARM: Temperature is HIGH!");
      f.println("2, 8149979689, , 9030123253, 51856452185, 8149979689, ALARM: Pump oil Temperature is HIGH!");
      f.println("3, 8149979689, , , 51856452185, 7111111111, Return to normal: Temperature is back to normal.");
      f.println("4, 8149979689, , , , 7111111111, ALARM: Pump oil Temperature is back to normal.");
      f.println("5, 8149979689, , , , , ALARM: Speed is HIGH!");
      f.println("6, , , , , , Return to normal: Speed is back to normal.");
      f.close();
    }
  }

  Shared_unlockFileSystem();
}

void printMBMapSummary() {
  ensureMBMapConfigFile();
  Shared_loadMessageConfig();
  Serial.printf("[MBMAP] Loaded %u message entries from MBmapconf.csv\n",(unsigned)Shared_getLoadedMessageCount());
}

void printAPStatus() {
  Serial.println("");
  Serial.println("=== AP Mode Info ===");
  Serial.println("To enable AP Mode: Press and hold button on GPIO 33");
  Serial.println("AP SSID: ESP32_FileServer");
  Serial.println("AP Password: 12345678");
  Serial.println("AP URL: http://192.168.4.1");
  Serial.println("Note: AP mode not active by default");
}

void setupWebServerRoutes() {
  if (serverRoutesSetup) return;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlPage());
  });

  server.on("/api/download-csv/mbmapconf", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!Shared_lockFileSystem()) {
      request->send(503, "text/plain", "File system busy");
      return;
    }
    bool exists = LittleFS.exists("/MBmapconf.csv");
    Shared_unlockFileSystem();

    if (!exists) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    request->send(LittleFS, "/MBmapconf.csv", "text/csv", true);
  });

  server.on(
    "/api/upload-csv/mbmapconf",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      (void)filename;

      if (!index) {
        // First chunk — acquire filesystem lock
        if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) {
          request->send(503, "application/json", "{\"error\":\"File system busy\"}");
          return;
        }

        uploadFile = LittleFS.open("/MBmapconf.csv", "w");
        if (!uploadFile) {
          // File open failed — MUST release mutex before returning
          Shared_unlockFileSystem();
          request->send(500, "application/json", "{\"error\":\"File open failed\"}");
          return;
        }
      }

      if (len && uploadFile) {
        uploadFile.write(data, len);
      }

      if (final) {
        if (uploadFile) uploadFile.close();
        Shared_unlockFileSystem();

        bool loaded = Shared_loadMessageConfig();
        request->send(loaded ? 200 : 500,
                      "application/json",
                      loaded ? "{\"success\":true}" : "{\"error\":\"Reload failed\"}");
      }
    });

  server.on("/api/delete-csv/mbmapconf", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!Shared_lockFileSystem()) {
      request->send(503, "application/json", "{\"error\":\"File system busy\"}");
      return;
    }

    bool removed = LittleFS.exists("/MBmapconf.csv") && LittleFS.remove("/MBmapconf.csv");
    Shared_unlockFileSystem();
    Shared_loadMessageConfig();

    request->send(removed ? 200 : 404,
                  "application/json",
                  removed ? "{\"success\":true}" : "{\"error\":\"File not found\"}");
  });

  server.on("/api/config-summary", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"loaded\":" + String((unsigned)Shared_getLoadedMessageCount()) + "}";
    request->send(200, "application/json", json);
  });

  serverRoutesSetup = true;
}

void startAPMode() {
  if (Shared_isAPModeActive()) return;

  Serial.println("[AP] Starting Access Point...");

  WiFi.mode(WIFI_AP);
  delay(50);
  WiFi.softAP("ESP32_FileServer", "12345678");
  delay(200);

  Serial.print("[AP] AP IP address: ");
  Serial.println(WiFi.softAPIP());

  setupWebServerRoutes();

  if (!serverStarted) {
    server.begin();
    serverStarted = true;
  }

  Shared_setAPModeActive(true);
  Serial.println("[AP] Access Point is now active");
}

void stopAPMode() {
  if (!Shared_isAPModeActive()) return;

  Serial.println("[AP] Stopping Access Point...");

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Stop the server so it doesn't keep routes active on a dead AP
  if (serverStarted) {
    server.end();
    serverStarted = false;
    // Allow routes to be re-registered on next startAPMode()
    serverRoutesSetup = false;
  }

  Shared_setAPModeActive(false);
  Serial.println("[AP] Access Point is now disabled");
}

void AP_taskLoop(void *pvParameters) {
  (void)pvParameters;
  static unsigned long lastStateChange = 0;

  for (;;) {
    // Level-triggered latching switch:
    // LOW  = switch latched ON  → AP should be active
    // HIGH = switch latched OFF → AP should be inactive
    bool switchState = digitalRead(BUTTON_PIN);
    unsigned long now = millis();

    if (now - lastStateChange > BUTTON_DEBOUNCE_MS) {
      if (switchState == LOW && !Shared_isAPModeActive()) {
        startAPMode();
        lastStateChange = now;
      } else if (switchState == HIGH && Shared_isAPModeActive()) {
        stopAPMode();
        lastStateChange = now;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

String htmlPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>MB Map Config</title>
<style>
body { font-family: Arial, sans-serif; background: #f3f5f7; margin: 0; padding: 24px; }
.card { max-width: 720px; margin: 0 auto; background: white; border-radius: 12px; padding: 24px; box-shadow: 0 10px 30px rgba(0,0,0,0.08); }
h1 { margin-top: 0; }
button { width: 100%; padding: 12px; margin-top: 10px; border: 0; border-radius: 8px; cursor: pointer; font-size: 14px; }
.primary { background: #1565c0; color: white; }
.danger  { background: #c62828; color: white; }
.muted   { color: #555; font-size: 14px; }
#status  { margin-top: 16px; font-size: 14px; color: #333; }
</style>
</head>
<body>
<div class="card">
  <h1>MB Map Config</h1>
  <p class="muted">Manage the <code>MBmapconf.csv</code> file used for Modbus-triggered SMS messages.</p>
  <button class="primary" onclick="window.open('/api/download-csv/mbmapconf')">Download MBmapconf.csv</button>
  <button class="primary" onclick="document.getElementById('file').click()">Upload MBmapconf.csv</button>
  <button class="danger"  onclick="deleteConfig()">Delete MBmapconf.csv</button>
  <p id="summary" class="muted">Loading summary...</p>
  <div id="status"></div>
  <input id="file" type="file" accept=".csv" style="display:none" onchange="uploadFile()">
</div>
<script>
function setStatus(msg) { document.getElementById('status').textContent = msg; }
function refreshSummary() {
  fetch('/api/config-summary')
    .then(r => r.json())
    .then(data => document.getElementById('summary').textContent = 'Loaded message entries: ' + data.loaded)
    .catch(err => setStatus('Summary failed: ' + err.message));
}
function uploadFile() {
  const file = document.getElementById('file').files[0];
  if (!file) return;
  const formData = new FormData();
  formData.append('file', file);
  fetch('/api/upload-csv/mbmapconf', { method: 'POST', body: formData })
    .then(r => r.json())
    .then(() => { setStatus('Upload successful'); refreshSummary(); })
    .catch(err => setStatus('Upload failed: ' + err.message));
}
function deleteConfig() {
  fetch('/api/delete-csv/mbmapconf', { method: 'POST' })
    .then(r => r.json())
    .then(() => { setStatus('Delete complete'); refreshSummary(); })
    .catch(err => setStatus('Delete failed: ' + err.message));
}
refreshSummary();
</script>
</body>
</html>
)rawliteral";
}