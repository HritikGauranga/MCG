#include "AP.h"
#include "Shared.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

// Async server
AsyncWebServer server(80);

// upload file handle for server callbacks
static File uploadFile;

void createSampleCSVFiles() {
  // Sample phone numbers file
  if (!LittleFS.exists("/phone_numbers.csv")) {
    File f = LittleFS.open("/phone_numbers.csv", "w");
    f.println("PhoneNumber,Enabled,Name");
    f.println("+91XXXXXXXXXX,1,Admin");
    f.println("+91XXXXXXXXXX,1,Supervisor");
    f.println("+91XXXXXXXXXX,0,Service Technician");
    f.close();
    Serial.println("[CSV] Created phone_numbers.csv");
  }

  // Sample alert messages file
  if (!LittleFS.exists("/alert_messages.csv")) {
    File f = LittleFS.open("/alert_messages.csv", "w");
    f.println("AlertType,Template");
    f.println("TEMP_HIGH, ALERT: Temperature Critical! Current: {TEMP}°C (Limit: 50°C) - Action Required!");
    f.println("SPEED_HIGH, ALERT: Speed Exceeds Limit! Current: {SPEED} RPM (Limit: 150 RPM) - Check System!");
    f.println("PUMP_DURATION, ALERT: Pump Running Long! Duration: {DURATION} seconds (1hr limit) - Verify Operation!");
    f.close();
    Serial.println("[CSV] Created alert_messages.csv");
  }
}
void setupWebServerRoutes() {
  if (serverRoutesSetup) return;  // Already set up

  //  Dashboard
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlPage());
  });

  //  List files
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"files\":[";
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    bool first = true;

    while (file) {
      if (!first) json += ",";
      json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
      first = false;
      file = root.openNextFile();
    }

    json += "]}";
    request->send(200, "application/json", json);
  });

  //  Upload
  server.on(
    "/api/upload",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      //  Do nothing here
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {

      if (!index) {
        Serial.printf("Upload Start: %s\n", filename.c_str());

        if (filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
          Serial.println("Invalid filename!");
          return;
        }

        uploadFile = LittleFS.open("/" + filename, "w");

        if (!uploadFile) {
          Serial.println("File open failed!");
          return;
        }
      }

      if (len && uploadFile) {
        uploadFile.write(data, len);
      }

      if (final) {
        if (uploadFile) uploadFile.close();

        Serial.printf("Upload Done: %s (%u bytes)\n",
                      filename.c_str(), index + len);

        //  Send response AFTER upload completes
        request->send(200, "application/json", "{\"success\":true}");
      }
    });

  //  Download
  server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("filename")) {
      request->send(400, "text/plain", "Missing filename");
      return;
    }

    String filename = request->getParam("filename")->value();

    if (!LittleFS.exists("/" + filename)) {
      request->send(404, "text/plain", "File not found");
      return;
    }

    request->send(LittleFS, "/" + filename, "application/octet-stream", true);
  });

  //  Delete
  server.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("filename")) {
      request->send(400, "application/json", "{\"error\":\"No filename\"}");
      return;
    }

    String filename = request->getParam("filename")->value();

    if (LittleFS.remove("/" + filename)) {
      request->send(200, "application/json", "{\"success\":true}");
    } else {
      request->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    }
  });

  // CSV Phone Numbers Download
  server.on("/api/download-csv/phone_numbers", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/phone_numbers.csv")) {
      request->send(LittleFS, "/phone_numbers.csv", "text/csv", true);
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  // CSV Alert Messages Download
  server.on("/api/download-csv/alert_messages", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/alert_messages.csv")) {
      request->send(LittleFS, "/alert_messages.csv", "text/csv", true);
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  // CSV Phone Numbers Upload
  server.on(
    "/api/upload-csv/phone_numbers",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      
      if (!index) {
        Serial.println("Uploading phone_numbers.csv");
        uploadFile = LittleFS.open("/phone_numbers.csv", "w");
      }

      if (len && uploadFile) {
        uploadFile.write(data, len);
      }

      if (final) {
        if (uploadFile) uploadFile.close();
        request->send(200, "application/json", "{\"success\":true}");
        Serial.println("phone_numbers.csv updated");
      }
    });

  // CSV Alert Messages Upload
  server.on(
    "/api/upload-csv/alert_messages",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      
      if (!index) {
        Serial.println("Uploading alert_messages.csv");
        uploadFile = LittleFS.open("/alert_messages.csv", "w");
      }

      if (len && uploadFile) {
        uploadFile.write(data, len);
      }

      if (final) {
        if (uploadFile) uploadFile.close();
        request->send(200, "application/json", "{\"success\":true}");
        Serial.println("alert_messages.csv updated");
      }
    });

  // API: Get phone numbers status
  server.on("/api/phone-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"numbers\":[";
    File f = LittleFS.open("/phone_numbers.csv", "r");
    f.readStringUntil('\n');  // skip header

    bool first = true;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      if (line.length() == 0) continue;

      int comma1 = line.indexOf(',');
      int comma2 = line.indexOf(',', comma1 + 1);

      String number = line.substring(0, comma1);
      int enabled = line.substring(comma1 + 1, comma2).toInt();
      String name = line.substring(comma2 + 1);

      if (!first) json += ",";
      json += "{\"number\":\"" + number + "\",\"enabled\":" + (enabled ? "true" : "false") + ",\"name\":\"" + name + "\"}";
      first = false;
    }
    f.close();
    json += "]}";
    request->send(200, "application/json", json);
  });

  // API: Toggle phone number alert status
  server.on("/api/toggle-phone", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
      request->send(400, "application/json", "{\"error\":\"No number\"}");
      return;
    }

    String targetNumber = request->getParam("number")->value();
    String csvContent = "";
    File f = LittleFS.open("/phone_numbers.csv", "r");

    // Read header
    String header = f.readStringUntil('\n');
    csvContent += header + "\n";

    while (f.available()) {
      String line = f.readStringUntil('\n');
      if (line.length() == 0) continue;

      int comma1 = line.indexOf(',');
      String number = line.substring(0, comma1);

      if (number == targetNumber) {
        int comma2 = line.indexOf(',', comma1 + 1);
        int enabled = line.substring(comma1 + 1, comma2).toInt();
        String rest = line.substring(comma2);
        
        // Toggle enabled status
        csvContent += number + "," + (enabled ? "0" : "1") + rest + "\n";
      } else {
        csvContent += line + "\n";
      }
    }
    f.close();

    File fw = LittleFS.open("/phone_numbers.csv", "w");
    fw.print(csvContent); 
    
    fw.close();

    request->send(200, "application/json", "{\"success\":true}");
  });

  serverRoutesSetup = true;
  Serial.println("[WebServer] Routes configured");
}

void startAPMode() {
  if (apModeActive) return;  // Already running
  
  Serial.println("\n=== Starting AP Mode ===");
  
  // Switch to AP+STA mode to keep Ethernet working
  // (Ethernet doesn't use WiFi radio, but we set this for compatibility)
  WiFi.mode(WIFI_AP);
  delay(50);
  WiFi.softAP("ESP32_FileServer", "12345678");
  delay(200);  // Wait for AP to fully start
  
  Serial.println("Access Point Started!");
  Serial.print("SSID: ");
  Serial.println("ESP32_FileServer");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Set up and start web server routes
  setupWebServerRoutes();
  server.begin();
  Serial.println("WebUI available at: http://192.168.4.1");
  
  apModeActive = true;
}

void stopAPMode() {
  if (!apModeActive) return;  // Already stopped
  
  Serial.println("\n=== Stopping AP Mode ===");
  WiFi.softAPdisconnect(true);  // true = turn off AP
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  apModeActive = false;
  Serial.println("AP Mode disabled (switch to ON position to enable)");
}

// HTML page (copied from original main - kept as a single raw string)
String htmlPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 File Manager</title>
<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  min-height: 100vh;
  padding: 20px;
  display: flex;
  justify-content: center;
  align-items: center;
}

.container {
  background: white;
  border-radius: 12px;
  box-shadow: 0 10px 40px rgba(0,0,0,0.2);
  max-width: 800px;
  width: 100%;
  overflow: hidden;
}

.header {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  padding: 30px 20px;
  text-align: center;
}

.header h1 {
  font-size: 28px;
  margin-bottom: 8px;
}

.header p {
  font-size: 14px;
  opacity: 0.9;
}

.content {
  padding: 30px;
}

.upload-section {
  border: 2px dashed #667eea;
  border-radius: 8px;
  padding: 20px;
  text-align: center;
  margin-bottom: 30px;
  background: #f8f9ff;
  transition: all 0.3s ease;
}

.upload-section:hover {
  border-color: #764ba2;
  background: #f0f2ff;
}

.upload-section input[type="file"] {
  display: none;
}

.upload-label {
  display: inline-block;
  padding: 10px 20px;
  background: #667eea;
  color: white;
  border-radius: 6px;
  cursor: pointer;
  transition: background 0.3s ease;
  margin-right: 10px;
}

.upload-label:hover {
  background: #764ba2;
}

.upload-btn {
  padding: 10px 30px;
  background: #28a745;
  color: white;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  font-size: 14px;
  transition: background 0.3s ease;
}

.upload-btn:hover {
  background: #218838;
}

.upload-btn:disabled {
  background: #ccc;
  cursor: not-allowed;
}

.file-name {
  margin-top: 10px;
  font-size: 12px;
  color: #667eea;
}

.files-section h2 {
  color: #333;
  margin-bottom: 20px;
  font-size: 18px;
  border-bottom: 2px solid #667eea;
  padding-bottom: 10px;
}

.file-list {
  list-style: none;
}

.file-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 15px;
  background: #f8f9fa;
  border-radius: 6px;
  margin-bottom: 10px;
  border-left: 4px solid #667eea;
  transition: all 0.3s ease;
}

.file-item:hover {
  background: #e9ecef;
  transform: translateX(5px);
}

.file-info {
  flex: 1;
  text-align: left;
}

.file-name-text {
  font-weight: 600;
  color: #333;
  word-break: break-all;
}

.file-size {
  font-size: 12px;
  color: #999;
  margin-top: 5px;
}

.file-actions {
  display: flex;
  gap: 8px;
}

.btn-action {
  padding: 8px 12px;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
  transition: all 0.3s ease;
  white-space: nowrap;
}

.btn-download {
  background: #17a2b8;
  color: white;
}

.btn-download:hover {
  background: #138496;
}

.btn-delete {
  background: #dc3545;
  color: white;
}

.btn-delete:hover {
  background: #c82333;
}

.empty-state {
  text-align: center;
  padding: 40px 20px;
  color: #999;
}

.empty-state-icon {
  font-size: 48px;
  margin-bottom: 10px;
}

.status-message {
  padding: 12px;
  border-radius: 6px;
  margin-bottom: 15px;
  display: none;
}

.status-message.success {
  background: #d4edda;
  color: #155724;
  border: 1px solid #c3e6cb;
  display: block;
}

.status-message.error {
  background: #f8d7da;
  color: #721c24;
  border: 1px solid #f5c6cb;
  display: block;
}

.loading {
  display: inline-block;
  width: 12px;
  height: 12px;
  border: 2px solid #667eea;
  border-top: 2px solid transparent;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  0% { transform: rotate(0deg); }
  100% { transform: rotate(360deg); }
}

@media (max-width: 600px) {
  body {
    padding: 10px;
    min-height: 100vh;
  }

  .container {
    max-width: 100%;
    border-radius: 8px;
  }

  .header {
    padding: 20px 15px;
  }

  .header h1 {
    font-size: 22px;
    margin-bottom: 5px;
  }

  .header p {
    font-size: 12px;
  }

  .content {
    padding: 15px;
  }

  .upload-section {
    padding: 15px;
    margin-bottom: 20px;
    border-radius: 6px;
  }

  .upload-label {
    display: block;
    width: 100%;
    padding: 12px 15px;
    margin-bottom: 10px;
    margin-right: 0;
    text-align: center;
    border-radius: 6px;
  }

  .upload-btn {
    width: 100%;
    padding: 12px 20px;
    font-size: 14px;
    border-radius: 6px;
  }

  .upload-btn:disabled {
    width: 100%;
  }

  .file-name {
    font-size: 11px;
    margin-top: 8px;
    word-break: break-word;
  }

  .files-section h2 {
    font-size: 16px;
    margin-bottom: 15px;
    padding-bottom: 8px;
  }

  .file-item {
    flex-direction: column;
    align-items: flex-start;
    padding: 12px;
    margin-bottom: 8px;
    border-left-width: 3px;
  }

  .file-info {
    width: 100%;
    margin-bottom: 10px;
  }

  .file-name-text {
    font-size: 14px;
    font-weight: 600;
    word-break: break-all;
  }

  .file-size {
    font-size: 11px;
    margin-top: 4px;
  }

  .file-actions {
    width: 100%;
    gap: 6px;
    display: flex;
  }

  .btn-action {
    flex: 1;
    padding: 10px 8px;
    font-size: 13px;
    border-radius: 4px;
  }

  .status-message {
    padding: 10px;
    font-size: 13px;
    border-radius: 4px;
    margin-bottom: 12px;
  }

  .empty-state {
    padding: 30px 15px;
  }

  .empty-state-icon {
    font-size: 36px;
    margin-bottom: 8px;
  }

  .empty-state p {
    font-size: 13px;
  }
}

@media (max-width: 480px) {
  body {
    padding: 8px;
  }

  .container {
    border-radius: 6px;
  }

  .header {
    padding: 15px 10px;
  }

  .header h1 {
    font-size: 18px;
  }

  .header p {
    font-size: 11px;
  }

  .content {
    padding: 12px;
  }

  .upload-section {
    padding: 12px;
    margin-bottom: 15px;
  }

  .upload-label {
    padding: 10px 12px;
    font-size: 13px;
  }

  .upload-btn {
    padding: 10px 15px;
    font-size: 13px;
  }

  .files-section h2 {
    font-size: 15px;
    margin-bottom: 12px;
  }

  .file-item {
    padding: 10px;
    margin-bottom: 6px;
  }

  .file-name-text {
    font-size: 13px;
  }

  .btn-action {
    padding: 8px 6px;
    font-size: 12px;
  }

  .empty-state {
    padding: 25px 12px;
  }

  .empty-state-icon {
    font-size: 32px;
  }
}
</style>
</head>
<body>

<div class="container">
  <div class="header">
    <h1>📁 ESP32 File Manager</h1>
    <p>Organize and manage files on your ESP32</p>
  </div>

  <div class="content">
    <div class="status-message" id="statusMsg"></div>

    <div class="upload-section">
      <label for="file" class="upload-label">📤 Choose File</label>
      <button class="upload-btn" onclick="uploadFile()">Upload</button>
      <div class="file-name" id="fileName"></div>
    </div>

    <div class="files-section">
      <h2>📋 Recent Files</h2>
      <ul class="file-list" id="list">
        <div class="empty-state">
          <div class="empty-state-icon">📭</div>
          <p>No files yet. Upload one to get started!</p>
        </div>
      </ul>
    </div>

    <div class="files-section">
      <h2>📱 SMS Alert Management</h2>
      <div style="margin-bottom: 15px;">
        <strong>Phone Numbers CSV:</strong><br/>
        <button class="btn-action btn-download" onclick="downloadPhoneCSV()" style="margin-top:5px;">⬇️ Download phone_numbers.csv</button>
        <button class="btn-action btn-download" onclick="uploadPhoneCSVPrompt()" style="margin-top:5px;">📤 Upload phone_numbers.csv</button>
      </div>
      <div style="margin-bottom: 15px;">
        <strong>Alert Messages CSV:</strong><br/>
        <button class="btn-action btn-download" onclick="downloadAlertCSV()" style="margin-top:5px;">⬇️ Download alert_messages.csv</button>
        <button class="btn-action btn-download" onclick="uploadAlertCSVPrompt()" style="margin-top:5px;">📤 Upload alert_messages.csv</button>
      </div>
      <div>
        <strong>Active Phone Numbers:</strong>
        <ul class="file-list" id="phoneList" style="margin-top:10px;">
          <div class="empty-state" style="padding:15px;">Loading...</div>
        </ul>
      </div>
    </div>

    <input type="file" id="file" onchange="updateFileName()" style="display:none;">
    <input type="file" id="csvFile" onchange="uploadCSVFile()" style="display:none;" accept=".csv">
  </div>
</div>

<script>

function updateFileName() {
  const file = document.getElementById("file").files[0];
  const fileNameEl = document.getElementById("fileName");
  if (file) {
    fileNameEl.textContent = "Selected: " + file.name;
  } else {
    fileNameEl.textContent = "";
  }
}

function showStatus(msg, type) {
  const statusEl = document.getElementById("statusMsg");
  statusEl.textContent = msg;
  statusEl.className = "status-message " + type;
  setTimeout(() => {
    statusEl.className = "status-message";
  }, 3000);
}

function uploadFile() {
  let file = document.getElementById("file").files[0];
  
  if (!file) {
    showStatus("Please select a file first", "error");
    return;
  }

  let formData = new FormData();
  formData.append("file", file);

  const btn = event.target;
  btn.disabled = true;
  btn.innerHTML = '<span class="loading"></span> Uploading...';

  fetch("/api/upload", {
    method: "POST",
    body: formData
  })
  .then(r => r.json())
  .then(data => {
    showStatus("File uploaded successfully!", "success");
    document.getElementById("file").value = "";
    document.getElementById("fileName").textContent = "";
    load();
  })
  .catch(e => {
    showStatus("Upload failed: " + e.message, "error");
  })
  .finally(() => {
    btn.disabled = false;
    btn.innerHTML = "Upload";
  });
}

function load() {
  fetch("/api/files")
  .then(r => r.json())
  .then(data => {
    let html = "";
    
    if (data.files.length === 0) {
      html = `<div class="empty-state">
        <div class="empty-state-icon">📭</div>
        <p>No files yet. Upload one to get started!</p>
      </div>`;
    } else {
      data.files.forEach(f => {
        const sizeKB = (f.size / 1024).toFixed(2);
        html += `<li class="file-item">
          <div class="file-info">
            <div class="file-name-text">📄 ${f.name}</div>
            <div class="file-size">${sizeKB} KB</div>
          </div>
          <div class="file-actions">
            <button class="btn-action btn-download" onclick="download('${f.name.replace(/'/g, "\\'")}')">⬇️ Download</button>
            <button class="btn-action btn-delete" onclick="del('${f.name.replace(/'/g, "\\'")}')">🗑️ Delete</button>
          </div>
        </li>`;
      });
    }
    
    document.getElementById("list").innerHTML = html;
  })
  .catch(e => {
    showStatus("Failed to load files: " + e.message, "error");
  });
}

function download(name) {
  window.open("/api/download?filename=" + encodeURIComponent(name));
}

function del(name) {
  if (confirm("Are you sure you want to delete '" + name + "'?")) {
    fetch("/api/delete?filename=" + encodeURIComponent(name), {method: "DELETE"})
    .then(r => r.json())
    .then(data => {
      showStatus("File deleted successfully!", "success");
      load();
    })
    .catch(e => {
      showStatus("Delete failed: " + e.message, "error");
    });
  }
}

// ================= SMS MANAGEMENT FUNCTIONS =================
function downloadPhoneCSV() {
  window.open("/api/download-csv/phone_numbers");
}

function downloadAlertCSV() {
  window.open("/api/download-csv/alert_messages");
}

function uploadPhoneCSVPrompt() {
  document.getElementById("csvFile").dataset.type = "phone_numbers";
  document.getElementById("csvFile").click();
}

function uploadAlertCSVPrompt() {
  document.getElementById("csvFile").dataset.type = "alert_messages";
  document.getElementById("csvFile").click();
}

function uploadCSVFile() {
  let file = document.getElementById("csvFile").files[0];
  let fileType = document.getElementById("csvFile").dataset.type;
  
  if (!file) return;

  let formData = new FormData();
  formData.append("file", file);

  fetch("/api/upload-csv/" + fileType, {
    method: "POST",
    body: formData
  })
  .then(r => r.json())
  .then(data => {
    showStatus("CSV uploaded successfully!", "success");
    document.getElementById("csvFile").value = "";
    loadPhoneNumbers();
  })
  .catch(e => {
    showStatus("CSV upload failed: " + e.message, "error");
  });
}

function loadPhoneNumbers() {
  fetch("/api/phone-status")
  .then(r => r.json())
  .then(data => {
    let html = "";
    
    if (data.numbers.length === 0) {
      html = `<div class="empty-state" style="padding:15px;">No phone numbers configured</div>`;
    } else {
      data.numbers.forEach(item => {
        html += `<li class="file-item">
          <div class="file-info">
            <div class="file-name-text">📞 ${item.number}</div>
            <div class="file-size">${item.name}</div>
          </div>
          <div class="file-actions">
            <button class="btn-action ${item.enabled ? 'btn-download' : 'btn-delete'}" 
                    onclick="togglePhoneAlert('${item.number}')" 
                    style="width:100%;">
              ${item.enabled ? '✅ Enabled' : '❌ Disabled'}
            </button>
          </div>
        </li>`;
      });
    }
    
    document.getElementById("phoneList").innerHTML = html;
  })
  .catch(e => {
    document.getElementById("phoneList").innerHTML = `<div class="empty-state">Error loading: ${e.message}</div>`;
  });
}

function togglePhoneAlert(number) {
  fetch("/api/toggle-phone?number=" + encodeURIComponent(number), {
    method: "POST"
  })
  .then(r => r.json())
  .then(data => {
    loadPhoneNumbers();
  })
  .catch(e => {
    showStatus("Toggle failed: " + e.message, "error");
  });
}

// Load files on page load
load();
loadPhoneNumbers();

</script>

</body>
</html>
)rawliteral";
}
