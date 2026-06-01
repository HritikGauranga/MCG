#include "AP.h"
#include "Shared.h"
#include <ESPAsyncWebServer.h>
#include <ETH.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>

static AsyncWebServer server(80);
static File          uploadFile;
static bool          serverStarted     = false;
static bool          serverRoutesSetup = false;
static const char    *WEBUI_USER        = "Admin";
static const char    *WEBUI_PASS        = "Admin@123";
static const char    *AP_PASS_FIXED     = "MSys@1234";
static const char    *AUTH_COOKIE_NAME  = "MSMSG_AUTH";
static const char    *SERIAL_FILE_PATH  = "/serialnumber.txt";
static const char    *SERIAL_META_PATH  = "/serial_meta.txt";
static const char    *MBMAP_FILE_PATH   = "/MBmapconf.csv";
static String         gAuthSessionToken = "";
#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG __DATE__ " " __TIME__
#endif
static const char *FW_BUILD_TAG_VALUE = FW_BUILD_TAG;

static String htmlPage();
static void setupWebServerRoutes();
static void startAPMode();
static void stopAPMode();

static String makeSessionToken() {
  char buf[33] = {};
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  uint32_t c = esp_random();
  uint32_t d = esp_random();
  snprintf(buf, sizeof(buf), "%08lx%08lx%08lx%08lx",
           (unsigned long)a,
           (unsigned long)b,
           (unsigned long)c,
           (unsigned long)d);
  return String(buf);
}

static String readCookieValue(const String &cookieHeader, const String &name) {
  int start = 0;
  while (start < (int)cookieHeader.length()) {
    int sep = cookieHeader.indexOf(';', start);
    if (sep < 0) sep = cookieHeader.length();
    String pair = cookieHeader.substring(start, sep);
    pair.trim();

    int eq = pair.indexOf('=');
    if (eq > 0) {
      String key = pair.substring(0, eq);
      String val = pair.substring(eq + 1);
      key.trim();
      val.trim();
      if (key == name) return val;
    }
    start = sep + 1;
  }
  return "";
}

static bool isAuthenticated(AsyncWebServerRequest *request) {
  if (gAuthSessionToken.length() == 0) return false;
  if (!request->hasHeader("Cookie")) return false;
  String cookie = request->getHeader("Cookie")->value();
  String value = readCookieValue(cookie, String(AUTH_COOKIE_NAME));
  return value.length() > 0 && value == gAuthSessionToken;
}

static void sendRedirect(AsyncWebServerRequest *request, const char *location) {
  AsyncWebServerResponse *res = request->beginResponse(302);
  res->addHeader("Location", location);
  request->send(res);
}

static void clearAuthCookie(AsyncWebServerRequest *request) {
  gAuthSessionToken = "";
  AsyncWebServerResponse *res = request->beginResponse(302);
  res->addHeader("Location", "/login");
  res->addHeader("Set-Cookie", String(AUTH_COOKIE_NAME) + "=; Path=/; Max-Age=0");
  request->send(res);
}

static String loginPage(const String &prefilledUser, bool badCredentials = false) {
  String err = badCredentials ? "<div class='err'>Invalid ID or password.</div>" : "";
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Login</title>
<link rel="icon" type="image/png" href="/Gaurangalogo.png?v=2">
<style>
  * { box-sizing: border-box; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #eef2f7; font-family: Arial, sans-serif; }
  .panel { width: min(360px, 92vw); background: #fff; border-radius: 12px; box-shadow: 0 10px 30px rgba(0,0,0,0.08); padding: 24px; }
  h1 { margin: 0 0 14px; font-size: 20px; color: #1a1a2e; }
  label { display: block; font-size: 13px; color: #444; margin: 10px 0 6px; }
  input { width: 100%; padding: 10px 12px; border: 1px solid #d9dce2; border-radius: 8px; font-size: 14px; }
  .pass-wrap { position: relative; padding-right: 36px; }
  .pass-wrap input { padding-right: 12px; }
  .eye-btn {
    position: absolute; right: 0; top: 50%; transform: translateY(-50%);
    width: 32px; height: 32px; margin-top: 0;
    display: flex; align-items: center; justify-content: center;
    border: 0; background: transparent; cursor: pointer; font-size: 18px;
    line-height: 1; padding: 0; color: #444;
  }
  .eye-btn .eye-icon { position: relative; display: inline-block; }
  .eye-btn.eye-off .eye-icon::after {
    content: "";
    position: absolute;
    left: -1px;
    right: -1px;
    top: 50%;
    height: 2px;
    background: currentColor;
    transform: rotate(-35deg);
    transform-origin: center;
  }
  button { width: 100%; margin-top: 16px; padding: 10px 12px; border: 0; border-radius: 8px; background: #1565c0; color: #fff; font-weight: 600; cursor: pointer; }
  button:hover { opacity: 0.9; }
  .pass-wrap .eye-btn {
    width: 32px;
    margin-top: 0;
    padding: 0;
    background: transparent;
    border-radius: 0;
    color: #444;
  }
  .err { margin: 8px 0 6px; padding: 10px; border-radius: 8px; background: #ffebee; color: #c62828; border: 1px solid #ef9a9a; font-size: 13px; }
</style>
</head>
<body>
  <form class="panel" method="POST" action="/login" autocomplete="off">
    <h1>MB Map Config Login</h1>
    __ERROR_BLOCK__
    <label for="user">ID</label>
    <input id="user" name="user" type="text" value="__PREFILLED_USER__" readonly required>
    <label for="pass">Password</label>
    <div class="pass-wrap">
      <input id="pass" name="pass" type="password" required>
      <button class="eye-btn eye-off" type="button" id="togglePass" aria-label="Show password"><span class="eye-icon">&#128065;</span></button>
    </div>
    <button type="submit">Login</button>
  </form>
<script>
  (function() {
    var pass = document.getElementById('pass');
    var btn = document.getElementById('togglePass');
    if (!pass || !btn) return;
    btn.addEventListener('click', function() {
      var show = pass.type === 'password';
      pass.type = show ? 'text' : 'password';
      btn.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
      if (show) btn.classList.remove('eye-off');
      else btn.classList.add("eye-off");
    });
  })();
</script>
</body>
</html>
)rawliteral";
  page.replace("__ERROR_BLOCK__", err);
  page.replace("__PREFILLED_USER__", prefilledUser);
  return page;
}

static String readSerialNumber() {
  String serial = "";
  if (!Shared_lockFileSystem()) return serial;

  if (LittleFS.exists(SERIAL_FILE_PATH)) {
    File f = LittleFS.open(SERIAL_FILE_PATH, "r");
    if (f) {
      serial = f.readStringUntil('\n');
      serial.trim();
      f.close();
    }
  }

  Shared_unlockFileSystem();
  return serial;
}

static String getAPSSID() {
  String serial = readSerialNumber();
  serial.trim();
  if (serial.length() == 0) return "MSys";
  return "MSys-" + serial;
}

static String readSerialMeta() {
  String meta = "";
  if (!Shared_lockFileSystem()) return meta;
  if (LittleFS.exists(SERIAL_META_PATH)) {
    File f = LittleFS.open(SERIAL_META_PATH, "r");
    if (f) {
      meta = f.readStringUntil('\n');
      meta.trim();
      f.close();
    }
  }
  Shared_unlockFileSystem();
  return meta;
}

static void clearStaleSerialForNewBuild() {
  if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) {
    Serial.println("[SERIAL] File system busy, skipping stale serial cleanup");
    return;
  }

  String serial = "";
  String meta = "";

  if (LittleFS.exists(SERIAL_FILE_PATH)) {
    File sf = LittleFS.open(SERIAL_FILE_PATH, "r");
    if (sf) {
      serial = sf.readStringUntil('\n');
      serial.trim();
      sf.close();
    }
  }

  if (LittleFS.exists(SERIAL_META_PATH)) {
    File mf = LittleFS.open(SERIAL_META_PATH, "r");
    if (mf) {
      meta = mf.readStringUntil('\n');
      meta.trim();
      mf.close();
    }
  }

  bool hasPreviousSerial = (serial.length() > 0);
  bool isCurrentBuildMeta = (meta == String(FW_BUILD_TAG_VALUE));

  if (hasPreviousSerial && !isCurrentBuildMeta) {
    LittleFS.remove(SERIAL_FILE_PATH);
    LittleFS.remove(SERIAL_META_PATH);
    Serial.println("[SERIAL] Cleared stale serial for new firmware build");
  }

  Shared_unlockFileSystem();
}

static String getLoginUsername() {
  return String(WEBUI_USER);
}

static String getLoginPassword() {
  return String(WEBUI_PASS);
}

static bool isSerialLockedForCurrentBuild() {
  String serial = readSerialNumber();
  if (serial.length() == 0) return false;
  String meta = readSerialMeta();
  return meta == String(FW_BUILD_TAG_VALUE);
}

static bool isSerialFormatValid(const String &serial) {
  if (serial.length() < 3 || serial.length() > 32) return false;

  for (size_t i = 0; i < serial.length(); ++i) {
    char c = serial.charAt(i);
    bool ok =
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') ||
      c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

static bool writeSerialNumberOnce(const String &serial, String &error) {
  if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) {
    error = "File system busy";
    return false;
  }

  bool lockedForThisBuild = false;
  if (LittleFS.exists(SERIAL_FILE_PATH)) {
    File existing = LittleFS.open(SERIAL_FILE_PATH, "r");
    if (existing) {
      String current = existing.readStringUntil('\n');
      current.trim();
      existing.close();
      if (current.length() > 0) {
        String meta = "";
        if (LittleFS.exists(SERIAL_META_PATH)) {
          File m = LittleFS.open(SERIAL_META_PATH, "r");
          if (m) {
            meta = m.readStringUntil('\n');
            meta.trim();
            m.close();
          }
        }
        lockedForThisBuild = (meta == String(FW_BUILD_TAG_VALUE));
      }
      if (current.length() > 0 && lockedForThisBuild) {
        Shared_unlockFileSystem();
        error = "Serial number already set and locked for this firmware build";
        return false;
      }
    }
  }

  File out = LittleFS.open(SERIAL_FILE_PATH, "w");
  if (!out) {
    Shared_unlockFileSystem();
    error = "Failed to open serial file";
    return false;
  }
  out.println(serial);
  out.close();

  File meta = LittleFS.open(SERIAL_META_PATH, "w");
  if (!meta) {
    Shared_unlockFileSystem();
    error = "Serial saved but failed to lock metadata";
    return false;
  }
  meta.println(FW_BUILD_TAG_VALUE);
  meta.close();

  Shared_unlockFileSystem();
  return true;
}

static String serialNumberPage(const String &currentSerial, const String &message, bool okMessage) {
  String status = "";
  if (message.length() > 0) {
    status = "<div class='status " + String(okMessage ? "ok" : "err") + "'>" + message + "</div>";
  }

  String formBlock = "";
  if (currentSerial.length() > 0 && isSerialLockedForCurrentBuild()) {
    formBlock = "<div class='locked'>Serial Number is <strong>" + currentSerial + "</strong></div>";
  } else if (currentSerial.length() > 0) {
    formBlock = "<div class='status ok'>Previous serial found: <strong>" + currentSerial + "</strong>. You can overwrite it once for this new firmware upload.</div>" + String(R"rawliteral(
      <form method="POST" action="/serialnumber/">
        <label for="serial">Serial Number</label>
        <input id="serial" name="serial" type="text" required maxlength="32" pattern="[A-Za-z0-9_-]+">
        <button type="submit">Save Serial Number</button>
      </form>
    )rawliteral");
  } else {  
    formBlock = R"rawliteral(
      <form method="POST" action="/serialnumber/">
        <label for="serial">Serial Number</label>
        <input id="serial" name="serial" type="text" placeholder="e.g. MCG001" required maxlength="32" pattern="[A-Za-z0-9_-]+">
        <button type="submit">Set Serial Number</button>
      </form>
    )rawliteral";
  }

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Device Serial Number</title>
<link rel="icon" type="image/png" href="/Gaurangalogo.png?v=2">
<style>
  * { box-sizing: border-box; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #eef2f7; font-family: Arial, sans-serif; padding: 14px; }
  .panel { width: min(460px, 96vw); background: #fff; border-radius: 12px; box-shadow: 0 10px 30px rgba(0,0,0,0.08); padding: 24px; }
  h1 { margin: 0 0 14px; font-size: 20px; color: #1a1a2e; }
  p { margin: 0 0 12px; color: #555; font-size: 14px; }
  label { display: block; font-size: 13px; color: #444; margin: 10px 0 6px; }
  input { width: 100%; padding: 10px 12px; border: 1px solid #d9dce2; border-radius: 8px; font-size: 14px; }
  button { margin-top: 14px; padding: 10px 14px; border: 0; border-radius: 8px; background: #1565c0; color: #fff; font-weight: 600; cursor: pointer; }
  .status { margin: 10px 0 12px; padding: 10px; border-radius: 8px; font-size: 13px; }
  .status.ok { background: #e8f5e9; color: #2e7d32; border: 1px solid #a5d6a7; }
  .status.err { background: #ffebee; color: #c62828; border: 1px solid #ef9a9a; }
  .locked { margin: 12px 0; padding: 12px; border-radius: 8px; background: #f1f8e9; border: 1px solid #c5e1a5; color: #33691e; }
  .links { margin-top: 16px; }
  .links a { color: #1565c0; text-decoration: none; margin-right: 14px; font-size: 14px; }
</style>
</head>
<body>
 <div class="panel">
    <h1>Device Serial Number</h1>
    <p>This value can be written once. After setting, it becomes read-only.</p>
    __STATUS_BLOCK__
    __FORM_BLOCK__
    <div class="links">
      <a href="/">Back to Config</a>
      <a href="/logout">Logout</a>
    </div>
  </div>
</body>
</html>
)rawliteral";

  page.replace("__STATUS_BLOCK__", status);
  page.replace("__FORM_BLOCK__", formBlock);
  return page;
}

static String ipBytesToString(const uint8_t ip[4]) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

static bool parseIPFromText(const String &src, uint8_t out[4]) {
  int parts[4] = {0, 0, 0, 0};
  int p = 0;
  String token = "";
  for (size_t i = 0; i < src.length(); ++i) {
    char c = src.charAt(i);
    if (c == '.') {
      if (p > 2 || token.length() == 0) return false;
      parts[p++] = token.toInt();
      token = "";
      continue;
    }
    if (c < '0' || c > '9') return false;
    token += c;
  }
  if (p != 3 || token.length() == 0) return false;
  parts[3] = token.toInt();
  for (int i = 0; i < 4; ++i) {
    if (parts[i] < 0 || parts[i] > 255) return false;
    out[i] = (uint8_t)parts[i];
  }
  return true;
}

static String gatewaySettingsPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Gateway Settings</title>
<link rel="icon" type="image/png" href="/Gaurangalogo.png?v=2">
<style>
  *{box-sizing:border-box}
  body{font-family:Arial,sans-serif;background:#f3f5f7;margin:0;padding:12px}
  .card{max-width:860px;margin:auto;background:#fff;border-radius:10px;padding:16px;box-shadow:0 8px 26px rgba(0,0,0,.08)}
  h1{margin:0 0 12px;font-size:20px}
  .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
  .grid>div{min-width:0}
  label{display:block;font-size:13px;color:#333;margin-bottom:6px}
  input,select{width:100%;max-width:100%;padding:8px;border:1px solid #cfd8dc;border-radius:7px}
  .full{grid-column:1/-1}
  .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
  .btn{padding:10px 14px;border:0;border-radius:8px;background:#1565c0;color:#fff;font-weight:600;cursor:pointer}
  .muted{font-size:12px;color:#666}
  .status{margin:10px 0;padding:10px;border-radius:8px;display:none}
  .ok{display:block;background:#e8f5e9;color:#2e7d32}
  .err{display:block;background:#ffebee;color:#c62828}
  @media (max-width:640px){
    body{padding:10px}
    .card{padding:12px}
    h1{font-size:18px}
    .grid{grid-template-columns:1fr;gap:10px}
    .btn{width:100%}
  }
</style>
</head>
<body>
<div class="card">
  <h1>Gateway Configuration</h1>
  <div id="status" class="status"></div>
  <div class="grid">
    <div class="full row">
      <input id="useDhcp" type="checkbox" style="width:auto">
      <label for="useDhcp" style="margin:0">Use DHCP for Ethernet</label>
    </div>
    <div><label>Static IP</label><input id="staticIp" placeholder="192.168.8.200" inputmode="numeric" pattern="[0-9.]+" maxlength="15" oninput="sanitizeIpInput(this)"></div>
    <div><label>Subnet Mask</label><input id="subnetMask" placeholder="255.255.255.0" inputmode="numeric" pattern="[0-9.]+" maxlength="15" oninput="sanitizeIpInput(this)"></div>
    <div><label>Gateway IP</label><input id="gatewayIp" placeholder="192.168.8.1" inputmode="numeric" pattern="[0-9.]+" maxlength="15" oninput="sanitizeIpInput(this)"></div>
    <div><label>TCP Port</label><input id="tcpPort" type="number" min="1" max="65535" inputmode="numeric" oninput="sanitizeNumberInput(this)"></div>
    <div><label>RTU Slave ID</label><input id="slaveId" type="number" min="1" max="254" inputmode="numeric" oninput="sanitizeNumberInput(this)"></div>
    <div><label>RTU Baud Rate</label>
      <select id="baudRate">
        <option>9600</option><option>19200</option><option>38400</option><option>57600</option><option>115200</option>
      </select>
    </div>
    <div><label>Data Bits</label><select id="dataBits"><option>7</option><option selected>8</option></select></div>
    <div><label>Parity</label><select id="parity"><option>N</option><option>E</option><option>O</option></select></div>
    <div><label>Stop Bits</label><select id="stopBits"><option selected>1</option><option>2</option></select></div>
  </div>
  <div class="row" style="margin-top:14px">
    <button class="btn" onclick="saveCfg()">Save Settings</button>
    <a href="/" class="muted">Back to MB Config</a>
  </div>
  <p class="muted">After save, reboot device to apply RTU/TCP stack settings.</p>
</div>
<script>
function status(msg, ok){var s=document.getElementById('status');s.textContent=msg;s.className='status '+(ok?'ok':'err');}
function sanitizeIpInput(el){
  if(!el) return;
  var cleaned = el.value.replace(/[^0-9.]/g, '');
  var parts = cleaned.split('.');
  if (parts.length > 4) parts = parts.slice(0, 4);
  for (var i = 0; i < parts.length; i++) {
    if (parts[i].length > 3) parts[i] = parts[i].slice(0, 3);
  }
  el.value = parts.join('.');
}
function sanitizeNumberInput(el){
  if(!el) return;
  el.value = el.value.replace(/[^0-9]/g, '');
  if (el.id === 'slaveId') {
    if (el.value === '') return;
    var v = parseInt(el.value, 10);
    if (!Number.isInteger(v)) return;
    if (v < 1) v = 1;
    if (v > 254) v = 254;
    el.value = String(v);
  }
}
function loadCfg(){
  fetch('/api/gateway-settings').then(r=>r.json()).then(c=>{
    document.getElementById('useDhcp').checked=!!c.use_dhcp;
    document.getElementById('staticIp').value=c.static_ip||'';
    document.getElementById('subnetMask').value=c.subnet_mask||'';
    document.getElementById('gatewayIp').value=c.gateway_ip||'';
    document.getElementById('tcpPort').value=c.tcp_port;
    document.getElementById('slaveId').value=c.slave_id;
    document.getElementById('baudRate').value=String(c.baud_rate);
    document.getElementById('dataBits').value=String(c.data_bits);
    document.getElementById('parity').value=c.parity;
    document.getElementById('stopBits').value=String(c.stop_bits);
  }).catch(e=>status('Load failed: '+e.message,false));
}
function saveCfg(){
  var slaveIdEl = document.getElementById('slaveId');
  var slaveId = parseInt(slaveIdEl.value, 10);
  if (!Number.isInteger(slaveId) || slaveId < 1 || slaveId > 254) {
    status('RTU Slave ID must be between 1 and 254.', false);
    slaveIdEl.focus();
    return;
  }

  var p=new URLSearchParams();
  p.append('use_dhcp',document.getElementById('useDhcp').checked?'1':'0');
  p.append('static_ip',document.getElementById('staticIp').value.trim());
  p.append('subnet_mask',document.getElementById('subnetMask').value.trim());
  p.append('gateway_ip',document.getElementById('gatewayIp').value.trim());
  p.append('tcp_port',document.getElementById('tcpPort').value);
  p.append('slave_id',String(slaveId));
  p.append('baud_rate',document.getElementById('baudRate').value);
  p.append('data_bits',document.getElementById('dataBits').value);
  p.append('parity',document.getElementById('parity').value);
  p.append('stop_bits',document.getElementById('stopBits').value);
  fetch('/api/gateway-settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(r=>r.json()).then(d=>{ if(d.success) status('Saved. Reboot to apply.',true); else status(d.error||'Save failed',false); })
    .catch(e=>status('Save failed: '+e.message,false));
}
loadCfg();
</script>
</body>
</html>
)rawliteral";
}

void ensureMBMapConfigFile() {
  if (!Shared_lockFileSystem()) return;

  if (!LittleFS.exists(MBMAP_FILE_PATH)) {
    File f = LittleFS.open(MBMAP_FILE_PATH, "w");
    if (f) {
      f.println("Msg.No., Phone number1, Phone number2, Phone number3, Phone number4, Phone number5, Text message");
      f.println("1, 0000000000, 0000000000, 0000000000, 0000000000, 0000000000, ALARM: Temperature is HIGH!");
      f.println("2, 0000000000, 0000000000, 0000000000, 0000000000, 0000000000, ALARM: Pump oil Temperature is HIGH!");
      f.println("3, 0000000000, 0000000000, 0000000000, 0000000000, 0000000000, Return to normal: Temperature is back to normal.");
      f.println("4, 0000000000, 0000000000, 0000000000, 0000000000, 0000000000, ALARM: Pump oil Temperature is back to normal.");
      f.println("5, 0000000000, 0000000000, 0000000000, 0000000000, 0000000000, ALARM: Speed is HIGH!");
      f.println("6, 0000000000, 0000000000, 0000000000, 0000000000, 0000000000, Return to normal: Speed is back to normal.");
      f.close();
    }
  }

  Shared_unlockFileSystem();
}

void printMBMapSummary() {
  Serial.printf("[MBMAP] Loaded %u message entries from MBmapconf.csv\n",
                (unsigned)Shared_getLoadedMessageCount());
}

void printAPStatus() {
  Serial.println("");
  Serial.println("=== AP Mode Info ===");
  Serial.println("To enable AP Mode: Press and hold button on GPIO 33");
  Serial.println("AP status LED: ON when AP mode is active");
  Serial.println("AP SSID: MSys or MSys-<SerialNumber>");
  Serial.println("AP Password: MSys@1234");
  Serial.println("AP URL: http://10.10.10.10");
  Serial.println("Note: AP mode not active by default");
}

// ---------------------------------------------------------------------------
// Build a JSON array of all loaded message configs for the table view.
// Format: [{"no":1,"phones":["081...","","","",""],"text":"ALARM..."},...]
// ---------------------------------------------------------------------------
static String buildConfigTableJSON() {
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

static void setupWebServerRoutes() {
  if (serverRoutesSetup) return;

  server.on("/Gaurangalogo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/Gaurangalogo.png", "image/png");
  });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
      sendRedirect(request, "/");
      return;
    }
    bool bad = request->hasParam("err");
    request->send(200, "text/html", loginPage(getLoginUsername(), bad));
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    String user = request->hasParam("user", true) ? request->getParam("user", true)->value() : "";
    String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
    String expectedUser = getLoginUsername();
    String expectedPass = getLoginPassword();

    if (user == expectedUser && pass == expectedPass) {
      gAuthSessionToken = makeSessionToken();
      AsyncWebServerResponse *res = request->beginResponse(302);
      res->addHeader("Location", "/");
      res->addHeader("Set-Cookie",
                     String(AUTH_COOKIE_NAME) + "=" + gAuthSessionToken +
                     "; Path=/; Max-Age=86400; HttpOnly; SameSite=Strict");
      request->send(res);
      return;
    }

    sendRedirect(request, "/login?err=1");
  });

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    clearAuthCookie(request);
  });

  server.on("/serialnumber", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      sendRedirect(request, "/login");
      return;
    }
    String serial = readSerialNumber();
    request->send(200, "text/html", serialNumberPage(serial, "", true));
  });

  server.on("/serialnumber/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      sendRedirect(request, "/login");
      return;
    }
    String serial = readSerialNumber();
    request->send(200, "text/html", serialNumberPage(serial, "", true));
  });

  server.on("/serialnumber/", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }

    String serial = request->hasParam("serial", true) ? request->getParam("serial", true)->value() : "";
    serial.trim();

    if (!isSerialFormatValid(serial)) {
      String current = readSerialNumber();
      request->send(400, "text/html", serialNumberPage(current, "Invalid serial format. Use only A-Z, a-z, 0-9, _ or - (3-32 chars).", false));
      return;
    }

    String error = "";
    if (!writeSerialNumberOnce(serial, error)) {
      String current = readSerialNumber();
      request->send(409, "text/html", serialNumberPage(current, error, false));
      return;
    }

    String current = readSerialNumber();
    String msg = "Serial number saved and locked successfully.";
    request->send(200, "text/html", serialNumberPage(current, msg, true));
  });

  server.on("/gateway-config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      sendRedirect(request, "/login");
      return;
    }
    request->send(200, "text/html", gatewaySettingsPage());
  });

  server.on("/gateway-config/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      sendRedirect(request, "/login");
      return;
    }
    request->send(200, "text/html", gatewaySettingsPage());
  });

  server.on("/api/gateway-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    GatewaySettings s = {};
    if (!Shared_getGatewaySettings(s)) {
      request->send(500, "application/json", "{\"error\":\"Read failed\"}");
      return;
    }
    String body = "{";
    body += "\"use_dhcp\":" + String(s.useDhcp ? "true" : "false") + ",";
    body += "\"static_ip\":\"" + ipBytesToString(s.staticIp) + "\",";
    body += "\"subnet_mask\":\"" + ipBytesToString(s.subnetMask) + "\",";
    body += "\"gateway_ip\":\"" + ipBytesToString(s.gatewayIp) + "\",";
    body += "\"tcp_port\":" + String(s.tcpPort) + ",";
    body += "\"slave_id\":" + String(s.slaveId) + ",";
    body += "\"baud_rate\":" + String((unsigned long)s.baudRate) + ",";
    body += "\"data_bits\":" + String(s.dataBits) + ",";
    body += "\"parity\":\"" + String(s.parity) + "\",";
    body += "\"stop_bits\":" + String(s.stopBits);
    body += "}";
    request->send(200, "application/json", body);
  });

  server.on("/api/gateway-settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }

    auto val = [&](const char *k) -> String {
      return request->hasParam(k, true) ? request->getParam(k, true)->value() : "";
    };
    GatewaySettings s = {};
    Shared_getGatewaySettings(s);

    s.useDhcp = (val("use_dhcp") == "1");
    String tcpPortText = val("tcp_port");
    String slaveIdText = val("slave_id");
    if (tcpPortText.startsWith("-") || slaveIdText.startsWith("-")) {
      request->send(400, "application/json", "{\"error\":\"Negative values are not allowed\"}");
      return;
    }
    long tcpPortLong = tcpPortText.toInt();
    long slaveIdLong = slaveIdText.toInt();
    if (tcpPortLong < 1 || tcpPortLong > 65535 || slaveIdLong < 1 || slaveIdLong > 254) {
      request->send(400, "application/json", "{\"error\":\"RTU Slave ID must be 1-254 and TCP Port must be 1-65535\"}");
      return;
    }
    s.tcpPort = (uint16_t)tcpPortLong;
    s.slaveId = (uint8_t)slaveIdLong;
    s.baudRate = (uint32_t)val("baud_rate").toInt();
    s.dataBits = (uint8_t)val("data_bits").toInt();
    String parity = val("parity");
    s.parity = parity.length() > 0 ? parity.charAt(0) : 'N';
    s.stopBits = (uint8_t)val("stop_bits").toInt();

    if (!(s.dataBits == 7 || s.dataBits == 8) ||
        !(s.parity == 'N' || s.parity == 'E' || s.parity == 'O') ||
        !(s.stopBits == 1 || s.stopBits == 2)) {
      request->send(400, "application/json", "{\"error\":\"Invalid RTU/TCP values\"}");
      return;
    }

    if (!parseIPFromText(val("static_ip"), s.staticIp) ||
        !parseIPFromText(val("subnet_mask"), s.subnetMask) ||
        !parseIPFromText(val("gateway_ip"), s.gatewayIp)) {
      request->send(400, "application/json", "{\"error\":\"Invalid IP format\"}");
      return;
    }

    if (!Shared_saveGatewaySettings(s)) {
      request->send(500, "application/json", "{\"error\":\"Save failed\"}");
      return;
    }
    request->send(200, "application/json", "{\"success\":true}");
  });

  server.on("/api/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }

    GatewaySettings s = {};
    if (!Shared_getGatewaySettings(s)) {
      request->send(500, "application/json", "{\"error\":\"Read failed\"}");
      return;
    }

    String serial = readSerialNumber();
    if (serial.length() == 0) serial = "Not Set";
    String apIp = WiFi.softAPIP().toString();
    if (apIp == "0.0.0.0") apIp = "AP Mode Off (10.10.10.10 when enabled)";
    String modbusIp = ETH.localIP().toString();
    if (modbusIp == "0.0.0.0") {
      modbusIp = ipBytesToString(s.staticIp);
    }
    String modbusEndpoint = modbusIp;

    String body = "{";
    body += "\"serial_number\":\"" + serial + "\",";
    body += "\"login_user\":\"" + getLoginUsername() + "\",";
    body += "\"ap_ip\":\"" + apIp + "\",";
    body += "\"modbus_endpoint\":\"" + modbusEndpoint + "\",";
    body += "\"use_dhcp\":" + String(s.useDhcp ? "true" : "false") + ",";
    body += "\"static_ip\":\"" + ipBytesToString(s.staticIp) + "\",";
    body += "\"subnet_mask\":\"" + ipBytesToString(s.subnetMask) + "\",";
    body += "\"gateway_ip\":\"" + ipBytesToString(s.gatewayIp) + "\",";
    body += "\"tcp_port\":" + String(s.tcpPort) + ",";
    body += "\"slave_id\":" + String(s.slaveId) + ",";
    body += "\"baud_rate\":" + String((unsigned long)s.baudRate) + ",";
    body += "\"data_bits\":" + String(s.dataBits) + ",";
    body += "\"parity\":\"" + String(s.parity) + "\",";
    body += "\"stop_bits\":" + String(s.stopBits);
    body += "}";
    request->send(200, "application/json", body);
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
    bool exists = LittleFS.exists(MBMAP_FILE_PATH);
    Shared_unlockFileSystem();

    if (!exists) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    request->send(LittleFS, MBMAP_FILE_PATH, "text/csv", true);
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
      if (!index && filename != "MBmapconf.csv") {
        request->send(400, "application/json",
                      "{\"error\":\"Invalid file name. Use exactly MBmapconf.csv\"}");
        return;
      }

      if (!index) {
        if (!Shared_lockFileSystem(pdMS_TO_TICKS(2000))) {
          request->send(503, "application/json", "{\"error\":\"File system busy\"}");
          return;
        }
        uploadFile = LittleFS.open(MBMAP_FILE_PATH, "w");
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
          String truncatedRows = Shared_getTruncatedMessageRowsCSV();
          String invalidPhoneWarnings = Shared_getInvalidPhoneWarningsJSON();
          String faultyMessageRows = Shared_getFaultyMessageRowsCSV();
          size_t extraRows = Shared_getTruncatedExtraRowCount();
          String body = "{\"success\":true,\"loaded\":" +
                        String((unsigned)Shared_getLoadedMessageCount()) +
                        ",\"message_limit\":150,\"truncated_rows\":\"" + truncatedRows +
                        "\",\"faulty_message_rows\":\"" + faultyMessageRows +
                        "\",\"extra_rows_truncated\":" + String((unsigned)extraRows) +
                        ",\"invalid_phones\":" + invalidPhoneWarnings +
                        ",\"rows\":" + buildConfigTableJSON() + "}";
          request->send(200, "application/json", body);
        } else {
          String err = "Reload failed";
          err.replace("\\", "\\\\");
          err.replace("\"", "\\\"");
          request->send(400, "application/json", String("{\"error\":\"") + err + "\"}");
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
    bool removed = LittleFS.exists(MBMAP_FILE_PATH) && LittleFS.remove(MBMAP_FILE_PATH);
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
    String invalidPhoneWarnings = Shared_getInvalidPhoneWarningsJSON();
    String body = "{\"loaded\":" +
                  String((unsigned)Shared_getLoadedMessageCount()) +
                  ",\"invalid_phones\":" + invalidPhoneWarnings +
                  ",\"rows\":" + buildConfigTableJSON() + "}";
    request->send(200, "application/json", body);
  });

  serverRoutesSetup = true;
}

static void startAPMode() {
  if (Shared_isAPModeActive()) return;

  Serial.println("[AP] Starting Access Point...");
  WiFi.mode(WIFI_AP_STA);
  delay(100);  // Increased delay to let WiFi/Ethernet stack stabilize after mode change
  IPAddress apIP(10, 10, 10, 10);
  IPAddress gateway(10, 10, 10, 10);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);
  String ssid = getAPSSID();
  WiFi.softAP(ssid.c_str(), AP_PASS_FIXED);
  delay(200);

  Serial.print("[AP] SSID: ");
  Serial.println(ssid);
  Serial.print("[AP] Password: ");
  Serial.println(AP_PASS_FIXED);
  Serial.print("[AP] AP IP address: ");
  Serial.println(WiFi.softAPIP());

  Shared_setAPModeActive(true);
  digitalWrite(AP_STATUS_LED_PIN, HIGH);
  Serial.println("[AP] Access Point is now active");
}

static void stopAPMode() {
  if (!Shared_isAPModeActive()) return;

  Serial.println("[AP] Stopping Access Point...");
  WiFi.softAPdisconnect(true);
  // Keep STA mode alive so Web UI on Ethernet remains available.
  WiFi.mode(WIFI_STA);
  delay(200);  // Increased delay to let WiFi/Ethernet stack stabilize after mode change

  Shared_setAPModeActive(false);
  digitalWrite(AP_STATUS_LED_PIN, LOW);
  Serial.println("[AP] Access Point is now disabled");
}

void AP_taskLoop(void *pvParameters) {
  (void)pvParameters;
  static unsigned long lastStateChange = 0;

  // Bring up base Wi-Fi stack without connecting, required by Async web stack.
  WiFi.mode(WIFI_STA);
  delay(50);

  // Reset old serial state when firmware build changes.
  clearStaleSerialForNewBuild();

  // Keep Web UI always active (Ethernet IP + AP IP when AP mode is enabled).
  setupWebServerRoutes();
  if (!serverStarted) {
    server.begin();
    serverStarted = true;
    Serial.println("[WEB] Config server started on port 80 (always active)");
  }

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

static String htmlPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>DASHBOARD</title>
<link rel="icon" type="image/png" href="/Gaurangalogo.png?v=2">
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
  .note { margin: 0 0 12px; padding: 10px 12px; border-radius: 8px;
          background: #fff8e1; border: 1px solid #ffe082; color: #6d4c41;
          font-size: 13px; }
  .note.hidden { display: none; }
  .upload-warnings { margin: 0 0 12px; padding: 12px; border-radius: 8px;
                     background: #fff3e0; border-left: 4px solid #ff9800; color: #e65100;
                     font-size: 13px; }
  .upload-warnings.error { background: #ffebee; border-left-color: #c62828; color: #b71c1c; }
  .upload-warnings.hidden { display: none; }
  .upload-warnings ul { margin: 8px 0 0; padding-left: 20px; }
  .upload-warnings li { margin: 4px 0; }
  .dashboard-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px 14px; margin-bottom: 18px; }
  .dash-item { background: #f8fbff; border: 1px solid #dce8f8; border-radius: 8px; padding: 10px 12px; min-width: 0; }
  .dash-label { display: block; font-size: 12px; color: #5f6c80; margin-bottom: 4px; }
  .dash-value { font-size: 14px; color: #1f2d3d; font-weight: 600; word-break: break-word; }
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
  .phone.phone-invalid { background: #ffebee; color: #b71c1c; border: 1px solid #ef9a9a; font-weight: 700; }
  .empty-row td { text-align: center; color: #aaa; padding: 24px; font-style: italic; }
  .count-badge { display: inline-block; background: #e8f0fe; color: #1565c0;
                 border-radius: 20px; padding: 2px 12px; font-size: 13px;
                 font-weight: 600; margin-left: 8px; }
  .dashboard-actions { margin: 2px 0 18px; }
  #file { display: none; }
  @media (max-width: 700px) {
    body { padding: 12px; }
    .card { padding: 14px; }
    .dashboard-grid { grid-template-columns: 1fr; }
  }
</style>
</head>
<body>
<div class="card">
  <h1>DASHBOARD</h1>

  <div class="actions">
    <button class="primary" onclick="openGatewayConfig()">Gateway Settings</button>
    <button class="danger"   onclick="logout()">Logout</button>
  </div>

  <div class="section-title">Current Configuration Status</div>
  <div id="dashboard" class="dashboard-grid">
    <div class="dash-item"><span class="dash-label">Serial Number</span><span id="dash-serial" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Login User Name</span><span id="dash-login-user" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Wi-Fi AP IP</span><span id="dash-ap-ip" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Modbus TCP Endpoint</span><span id="dash-modbus-endpoint" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">DHCP Mode</span><span id="dash-dhcp" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Static IP</span><span id="dash-static-ip" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Subnet Mask</span><span id="dash-subnet" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Gateway IP</span><span id="dash-gateway-ip" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">TCP Port</span><span id="dash-tcp-port" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">RTU Slave ID</span><span id="dash-slave-id" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">RTU Baud Rate</span><span id="dash-baud" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Data Bits</span><span id="dash-data-bits" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Parity</span><span id="dash-parity" class="dash-value">Loading...</span></div>
    <div class="dash-item"><span class="dash-label">Stop Bits</span><span id="dash-stop-bits" class="dash-value">Loading...</span></div>
  </div>
  <div class="dashboard-actions"></div>

  <div class="section-title">
    Loaded Entries
    <span id="count-badge" class="count-badge">0</span>
  </div>
  <div class="actions">
    <button class="primary"  onclick="document.getElementById('file').click()">&#8593; Upload CSV</button>
    <button class="primary"  onclick="downloadCSV()">&#8595; Download CSV</button>
    <button class="danger"   onclick="deleteConfig()">&#10005; Delete CSV</button>
  </div>
  <div id="upload-warnings" class="upload-warnings hidden"></div>

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
          <th class="msg-col">Text Message</th>
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
  if (!el) return;
  el.textContent = msg;
  el.className = 'status ' + type;
}

var latestInvalidPhones = [];

function displayUploadWarnings(data) {
  var warningsDiv = document.getElementById('upload-warnings');
  if (!warningsDiv) return;
  
  var warnings = [];
  var truncatedRows = (data.truncated_rows || '').trim();
  var faultyMessageRows = (data.faulty_message_rows || '').trim();
  var invalidPhones = Array.isArray(data.invalid_phones) ? data.invalid_phones : [];
  latestInvalidPhones = invalidPhones;
  var messageLimit = Number(data.message_limit || 150);
  var extraRowsTruncated = Number(data.extra_rows_truncated || 0);
  
  function toVisibleDataRows(csvRowsText) {
    if (!csvRowsText) return '';
    var out = csvRowsText
      .split(',')
      .map(function(s) { return Number(String(s).trim()); })
      .filter(function(n) { return Number.isFinite(n) && n >= 2; })
      .map(function(n) { return String(n - 1); });
    return out.join(',');
  }

  var truncatedRowsVisible = toVisibleDataRows(truncatedRows);
  var faultyMessageRowsVisible = toVisibleDataRows(faultyMessageRows);

  // Build warnings list
  if (truncatedRowsVisible.length > 0) {
    warnings.push('Text message character limit is ' + messageLimit + '. CSV row(s) ' + truncatedRowsVisible + ' were truncated.');
  }
  if (faultyMessageRowsVisible.length > 0) {
    warnings.push('Invalid message format detected in CSV row(s) ' + faultyMessageRowsVisible + '. Message text was left blank for those rows.');
  }

  if (extraRowsTruncated > 0) {
    warnings.push('CSV has more than 50 data rows. Extra row count truncated: ' + extraRowsTruncated + '.');
  }
  
  if (invalidPhones.length > 0) {
    var invalidRows = {};
    invalidPhones.forEach(function(w) {
      var no = Number(w && w.no);
      if (Number.isFinite(no) && no > 0) {
        invalidRows[no] = true;
      }
    });
    var rowList = Object.keys(invalidRows)
      .map(function(k) { return Number(k); })
      .sort(function(a, b) { return a - b; })
      .map(function(n) { return 'Row ' + n; });
    if (rowList.length > 0) {
      warnings.push('Invalid phone numbers detected in: ' + rowList.join(', '));
    }
  }
  
  // Display upload summary (with optional warnings)
  var html = '<strong>Upload successful - ' + data.loaded + ' entries loaded.</strong>'
           + '<br>Please verify all phone numbers in <strong>Loaded Entries</strong>. '
           + 'Invalid or non-existent numbers can cause SMS send failures.';
  if (warnings.length > 0) {
    html += '<br><strong>Warnings:</strong><ul>';
    warnings.forEach(function(w) {
      html += '<li>' + w + '</li>';
    });
    html += '</ul>';
  }
  warningsDiv.innerHTML = html;
  warningsDiv.className = 'upload-warnings';
}

function clearUploadWarnings() {
  var el = document.getElementById('upload-warnings');
  if (!el) return;
  el.className = 'upload-warnings hidden';
  el.innerHTML = '';
}

function showUploadError(msg) {
  var el = document.getElementById('upload-warnings');
  if (!el) return;
  el.innerHTML = '<strong>Upload failed:</strong> ' + escapeHtml(msg || 'unknown error');
  el.className = 'upload-warnings error';
}

function setDash(id, value) {
  var el = document.getElementById(id);
  if (!el) return;
  el.textContent = value;
}

function loadDashboard() {
  fetch('/api/dashboard')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      setDash('dash-serial', d.serial_number || 'Not Set');
      setDash('dash-login-user', d.login_user || 'Admin');
      setDash('dash-ap-ip', d.ap_ip || '-');
      setDash('dash-modbus-endpoint', d.modbus_endpoint || '-');
      setDash('dash-dhcp', d.use_dhcp ? 'Enabled' : 'Disabled');
      setDash('dash-static-ip', d.static_ip || '-');
      setDash('dash-subnet', d.subnet_mask || '-');
      setDash('dash-gateway-ip', d.gateway_ip || '-');
      setDash('dash-tcp-port', String(d.tcp_port || '-'));
      setDash('dash-slave-id', String(d.slave_id || '-'));
      setDash('dash-baud', String(d.baud_rate || '-'));
      setDash('dash-data-bits', String(d.data_bits || '-'));
      setDash('dash-parity', d.parity || '-');
      setDash('dash-stop-bits', String(d.stop_bits || '-'));
    })
    .catch(function(err) {
      setStatus('Failed to load dashboard: ' + err.message, 'err');
    });
}

function phone(num) {
  if (!num || num.trim() === '') return '<span style="color:#bbb">-</span>';
  return '<span class="phone">' + escapeHtml(num) + '</span>';
}

function buildInvalidPhoneMap(invalidPhones) {
  var map = {};
  if (!Array.isArray(invalidPhones)) return map;
  invalidPhones.forEach(function(w) {
    var no = Number(w && w.no);
    var col = String((w && w.col) || '');
    var m = col.match(/^Phone([1-5])$/);
    if (!Number.isFinite(no) || !m) return;
    var idx = Number(m[1]) - 1;
    var key = no + ':' + idx;
    map[key] = String((w && w.value) || '');
  });
  return map;
}

function phoneWithInvalid(num, invalidNum) {
  if (invalidNum && invalidNum.trim() !== '') {
    return '<span class="phone phone-invalid" title="Invalid phone number">' + escapeHtml(invalidNum) + '</span>';
  }
  return phone(num);
}

function renderTable(rows, invalidPhones) {
  var tbody = document.getElementById('table-body');
  var invalidMap = buildInvalidPhoneMap(invalidPhones || latestInvalidPhones);
  var safeRows = Array.isArray(rows) ? rows : [];
  safeRows = safeRows.filter(function(r) {
    if (!r) return false;
    var hasText = typeof r.text === 'string' && r.text.trim() !== '';
    var hasPhone = Array.isArray(r.phones) && r.phones.some(function(p) {
      return typeof p === 'string' && p.trim() !== '';
    });
    var hasInvalidPhone = false;
    if (Number.isFinite(Number(r.no))) {
      for (var ip = 0; ip < 5; ip++) {
        if (invalidMap[Number(r.no) + ':' + ip]) {
          hasInvalidPhone = true;
          break;
        }
      }
    }
    return hasText || hasPhone || hasInvalidPhone;
  });
  document.getElementById('count-badge').textContent = safeRows.length;

  if (safeRows.length === 0) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="7">No entries loaded. Upload a MBmapconf.csv file.</td></tr>';
    return;
  }

  var html = '';
  safeRows.forEach(function(r) {
    var rowNo = Number(r.no);
    html += '<tr>';
    html += '<td class="no-col">' + r.no + '</td>';
    for (var p = 0; p < 5; p++) {
      var invalidKey = rowNo + ':' + p;
      html += '<td>' + phoneWithInvalid(r.phones[p], invalidMap[invalidKey]) + '</td>';
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
    .then(function(data) {
      var invalidPhones = Array.isArray(data.invalid_phones) ? data.invalid_phones : [];
      latestInvalidPhones = invalidPhones;
      renderTable(data.rows, invalidPhones);
    })
    .catch(function(err) { setStatus('Failed to load config: ' + err.message, 'err'); });
}

function downloadCSV() {
  window.open('/api/download-csv/mbmapconf');
}

function uploadFile() {
  var file = document.getElementById('file').files[0];
  if (!file) return;
  if (file.name !== 'MBmapconf.csv') {
    showUploadError('Invalid file name. Please upload/rename the file exactly named as MBmapconf.csv');
    document.getElementById('file').value = '';
    return;
  }

  clearUploadWarnings();

  var formData = new FormData();
  formData.append('file', file);

  fetch('/api/upload-csv/mbmapconf', { method: 'POST', body: formData })
    .then(function(r) { return r.json(); })
    .then(function(data) {
      if (data.success) {
        displayUploadWarnings(data);
        renderTable(data.rows, data.invalid_phones);  // instant table update from upload response
      } else {
        showUploadError(data.error || 'unknown error');
      }
    })
    .catch(function(err) {
      showUploadError(err.message);
    });

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
        clearUploadWarnings();
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

function openGatewayConfig() {
  window.location.href = '/gateway-config';
}

// Load table on page open
loadDashboard();
loadTable();
</script>
</body>
</html>
)rawliteral";
}
