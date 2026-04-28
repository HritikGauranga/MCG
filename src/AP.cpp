#include "AP.h"
#include "Shared.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

static AsyncWebServer server(80);
static File           uploadFile;
static bool           serverStarted     = false;
static bool           serverRoutesSetup = false;
static const char    *WEBUI_USER        = "admin";
static const char    *WEBUI_PASS        = "admin123";
static const char    *AUTH_COOKIE_NAME  = "MSMSG_AUTH";
static const char    *AUTH_COOKIE_VALUE = "ok";

static bool isAuthenticated(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Cookie")) return false;
  String cookie = request->getHeader("Cookie")->value();
  String token  = String(AUTH_COOKIE_NAME) + "=" + AUTH_COOKIE_VALUE;
  return cookie.indexOf(token) >= 0;
}

static void sendRedirect(AsyncWebServerRequest *request, const char *location) {
  AsyncWebServerResponse *res = request->beginResponse(302);
  res->addHeader("Location", location);
  request->send(res);
}

static void clearAuthCookie(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *res = request->beginResponse(302);
  res->addHeader("Location", "/login");
  res->addHeader("Set-Cookie", String(AUTH_COOKIE_NAME) + "=; Path=/; Max-Age=0");
  request->send(res);
}

static String loginPage(bool badCredentials = false) {
  String err = badCredentials ? "<div class='err'>Invalid ID or password.</div>" : "";
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Login</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #eef2f7; font-family: Arial, sans-serif; }
  .panel { width: min(360px, 92vw); background: #fff; border-radius: 12px; box-shadow: 0 10px 30px rgba(0,0,0,0.08); padding: 24px; }
  h1 { margin: 0 0 14px; font-size: 20px; color: #1a1a2e; }
  label { display: block; font-size: 13px; color: #444; margin: 10px 0 6px; }
  input { width: 100%; padding: 10px 12px; border: 1px solid #d9dce2; border-radius: 8px; font-size: 14px; }
  button { width: 100%; margin-top: 16px; padding: 10px 12px; border: 0; border-radius: 8px; background: #1565c0; color: #fff; font-weight: 600; cursor: pointer; }
  button:hover { opacity: 0.9; }
  .err { margin: 8px 0 6px; padding: 10px; border-radius: 8px; background: #ffebee; color: #c62828; border: 1px solid #ef9a9a; font-size: 13px; }
</style>
</head>
<body>
  <form class="panel" method="POST" action="/login" autocomplete="off">
    <h1>MB Map Config Login</h1>
    __ERROR_BLOCK__
    <label for="user">ID</label>
    <input id="user" name="user" type="text" required>
    <label for="pass">Password</label>
    <input id="pass" name="pass" type="password" required>
    <button type="submit">Login</button>
  </form>
</body>
</html>
)rawliteral";
  page.replace("__ERROR_BLOCK__", err);
  return page;
}

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
  Serial.printf("[MBMAP] Loaded %u message entries from MBmapconf.csv\n",
                (unsigned)Shared_getLoadedMessageCount());
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

// ---------------------------------------------------------------------------
// Build a JSON array of all loaded message configs for the table view.
// Format: [{"no":1,"phones":["081...","","","",""],"text":"ALARM..."},...]
// ---------------------------------------------------------------------------
static String buildConfigTableJSON() {
  size_t count = Shared_getLoadedMessageCount();
  String json = "[";

  for (size_t i = 0; i < MESSAGE_SLOT_COUNT; ++i) {
    MessageConfig config = {};
    if (!Shared_getMessageConfig(i, config)) continue;

    if (json.length() > 1) json += ",";
    json += "{\"no\":" + String(config.msgNo) + ",\"phones\":[";

    for (size_t p = 0; p < PHONE_SLOTS_PER_MESSAGE; ++p) {
      if (p > 0) json += ",";
      json += "\"";
      json += String(config.phoneNumbers[p]);
      json += "\"";
    }

    json += "],\"text\":\"";
    // Escape quotes and backslashes in message text
    String text = String(config.text);
    text.replace("\\", "\\\\");
    text.replace("\"", "\\\"");
    json += text;
    json += "\"}";
  }

  json += "]";
  return json;
}

void setupWebServerRoutes() {
  if (serverRoutesSetup) return;

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
      sendRedirect(request, "/");
      return;
    }
    bool bad = request->hasParam("err");
    request->send(200, "text/html", loginPage(bad));
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    String user = request->hasParam("user", true) ? request->getParam("user", true)->value() : "";
    String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";

    if (user == WEBUI_USER && pass == WEBUI_PASS) {
      AsyncWebServerResponse *res = request->beginResponse(302);
      res->addHeader("Location", "/");
      res->addHeader("Set-Cookie",
                     String(AUTH_COOKIE_NAME) + "=" + AUTH_COOKIE_VALUE +
                     "; Path=/; Max-Age=86400; HttpOnly; SameSite=Lax");
      request->send(res);
      return;
    }

    sendRedirect(request, "/login?err=1");
  });

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    clearAuthCookie(request);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      sendRedirect(request, "/login");
      return;
    }
    request->send(200, "text/html", htmlPage());
  });

  server.on("/api/download-csv/mbmapconf", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
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
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      if (!isAuthenticated(request)) {
        if (!index) request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
      }
      (void)filename;

      if (!index) {
        if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) {
          request->send(503, "application/json", "{\"error\":\"File system busy\"}");
          return;
        }
        uploadFile = LittleFS.open("/MBmapconf.csv", "w");
        if (!uploadFile) {
          Shared_unlockFileSystem();
          request->send(500, "application/json", "{\"error\":\"File open failed\"}");
          return;
        }
      }

      if (len && uploadFile) uploadFile.write(data, len);

      if (final) {
        if (uploadFile) uploadFile.close();
        Shared_unlockFileSystem();

        bool loaded = Shared_loadMessageConfig();
        if (loaded) {
          // Return the parsed table data immediately so the page can
          // refresh the table without a separate fetch
          String body = "{\"success\":true,\"loaded\":" +
                        String((unsigned)Shared_getLoadedMessageCount()) +
                        ",\"rows\":" + buildConfigTableJSON() + "}";
          request->send(200, "application/json", body);
        } else {
          request->send(500, "application/json", "{\"error\":\"Reload failed\"}");
        }
      }
    });

  server.on("/api/delete-csv/mbmapconf", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
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

  // Returns current loaded config as JSON table rows
  server.on("/api/config-table", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    String body = "{\"loaded\":" +
                  String((unsigned)Shared_getLoadedMessageCount()) +
                  ",\"rows\":" + buildConfigTableJSON() + "}";
    request->send(200, "application/json", body);
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

  if (serverStarted) {
    server.end();
    serverStarted     = false;
    serverRoutesSetup = false;
  }

  Shared_setAPModeActive(false);
  Serial.println("[AP] Access Point is now disabled");
}

void AP_taskLoop(void *pvParameters) {
  (void)pvParameters;
  static unsigned long lastStateChange = 0;

  for (;;) {
    bool switchState  = digitalRead(BUTTON_PIN);
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
  * { box-sizing: border-box; }
  body { font-family: Arial, sans-serif; background: #f3f5f7; margin: 0; padding: 16px; }
  .card { max-width: 960px; margin: 0 auto; background: white; border-radius: 12px;
          padding: 24px; box-shadow: 0 10px 30px rgba(0,0,0,0.08); }
  h1 { margin-top: 0; font-size: 20px; color: #1a1a2e; }
  .actions { display: flex; gap: 10px; flex-wrap: wrap; margin-bottom: 20px; }
  button { padding: 10px 18px; border: 0; border-radius: 8px; cursor: pointer;
           font-size: 14px; font-weight: 600; transition: opacity 0.2s; }
  button:hover { opacity: 0.85; }
  .primary { background: #1565c0; color: white; }
  .success { background: #2e7d32; color: white; }
  .danger  { background: #c62828; color: white; }
  .status  { padding: 10px 14px; border-radius: 8px; font-size: 14px;
             margin-bottom: 16px; display: none; }
  .status.ok  { background: #e8f5e9; color: #2e7d32; border: 1px solid #a5d6a7; display: block; }
  .status.err { background: #ffebee; color: #c62828; border: 1px solid #ef9a9a; display: block; }
  .status.inf { background: #e3f2fd; color: #1565c0; border: 1px solid #90caf9; display: block; }
  .section-title { font-size: 15px; font-weight: 700; color: #333;
                   margin: 0 0 10px 0; padding-bottom: 6px;
                   border-bottom: 2px solid #e0e0e0; }
  .table-wrap { overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  thead tr { background: #1565c0; color: white; }
  thead th { padding: 10px 12px; text-align: left; font-weight: 600;
             white-space: nowrap; }
  tbody tr { border-bottom: 1px solid #f0f0f0; }
  tbody tr:hover { background: #f5f9ff; }
  tbody td { padding: 9px 12px; color: #333; vertical-align: top; }
  .no-col { width: 48px; text-align: center; font-weight: 700; color: #1565c0; }
  .msg-col { max-width: 260px; word-break: break-word; }
  .phone    { display: inline-block; background: #e8f0fe; color: #1a56db;
              border-radius: 4px; padding: 2px 7px; margin: 2px 2px 2px 0;
              font-size: 12px; white-space: nowrap; }
  .empty-row td { text-align: center; color: #aaa; padding: 24px; font-style: italic; }
  .count-badge { display: inline-block; background: #e8f0fe; color: #1565c0;
                 border-radius: 20px; padding: 2px 12px; font-size: 13px;
                 font-weight: 600; margin-left: 8px; }
  #file { display: none; }
</style>
</head>
<body>
<div class="card">
  <h1>MB Map Config</h1>

  <div class="actions">
    <button class="primary"  onclick="document.getElementById('file').click()">&#8593; Upload CSV</button>
    <button class="primary"  onclick="downloadCSV()">&#8595; Download CSV</button>
    <button class="danger"   onclick="deleteConfig()">&#10005; Delete CSV</button>
    <button class="danger"   onclick="logout()">Logout</button>
  </div>

  <div id="status" class="status"></div>

  <div class="section-title">
    Loaded Entries
    <span id="count-badge" class="count-badge">0</span>
  </div>

  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th class="no-col">No.</th>
          <th>Phone 1</th>
          <th>Phone 2</th>
          <th>Phone 3</th>
          <th>Phone 4</th>
          <th>Phone 5</th>
          <th class="msg-col">Message Text</th>
        </tr>
      </thead>
      <tbody id="table-body">
        <tr class="empty-row"><td colspan="7">Loading...</td></tr>
      </tbody>
    </table>
  </div>
</div>

<input id="file" type="file" accept=".csv" onchange="uploadFile()">

<script>
function setStatus(msg, type) {
  var el = document.getElementById('status');
  el.textContent = msg;
  el.className = 'status ' + type;
}

function clearStatus() {
  var el = document.getElementById('status');
  el.className = 'status';
}

function phone(num) {
  if (!num || num.trim() === '') return '<span style="color:#bbb">—</span>';
  return '<span class="phone">' + num + '</span>';
}

function renderTable(rows) {
  var tbody = document.getElementById('table-body');
  document.getElementById('count-badge').textContent = rows.length;

  if (!rows || rows.length === 0) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="7">No entries loaded. Upload a MBmapconf.csv file.</td></tr>';
    return;
  }

  var html = '';
  rows.forEach(function(r) {
    html += '<tr>';
    html += '<td class="no-col">' + r.no + '</td>';
    for (var p = 0; p < 5; p++) {
      html += '<td>' + phone(r.phones[p]) + '</td>';
    }
    html += '<td class="msg-col">' + escapeHtml(r.text) + '</td>';
    html += '</tr>';
  });
  tbody.innerHTML = html;
}

function escapeHtml(str) {
  return str.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function loadTable() {
  fetch('/api/config-table')
    .then(function(r) { return r.json(); })
    .then(function(data) { renderTable(data.rows); })
    .catch(function(err) { setStatus('Failed to load config: ' + err.message, 'err'); });
}

function downloadCSV() {
  window.open('/api/download-csv/mbmapconf');
}

function uploadFile() {
  var file = document.getElementById('file').files[0];
  if (!file) return;

  setStatus('Uploading...', 'inf');

  var formData = new FormData();
  formData.append('file', file);

  fetch('/api/upload-csv/mbmapconf', { method: 'POST', body: formData })
    .then(function(r) { return r.json(); })
    .then(function(data) {
      if (data.success) {
        setStatus('Upload successful — ' + data.loaded + ' entries loaded.', 'ok');
        renderTable(data.rows);  // instant table update from upload response
      } else {
        setStatus('Upload failed: ' + (data.error || 'unknown error'), 'err');
      }
    })
    .catch(function(err) { setStatus('Upload failed: ' + err.message, 'err'); });

  // Reset file input so the same file can be re-uploaded if needed
  document.getElementById('file').value = '';
}

function deleteConfig() {
  if (!confirm('Delete MBmapconf.csv? This cannot be undone.')) return;

  fetch('/api/delete-csv/mbmapconf', { method: 'POST' })
    .then(function(r) { return r.json(); })
    .then(function(data) {
      if (data.success) {
        setStatus('Config deleted.', 'ok');
        renderTable([]);
      } else {
        setStatus('Delete failed: ' + (data.error || 'unknown error'), 'err');
      }
    })
    .catch(function(err) { setStatus('Delete failed: ' + err.message, 'err'); });
}

function logout() {
  window.location.href = '/logout';
}

// Load table on page open
loadTable();
</script>
</body>
</html>
)rawliteral";
}
