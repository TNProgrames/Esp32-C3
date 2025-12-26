#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>  // <--- 1. إضافة مكتبة mDNS

#include "SensorManager.h"
#include "DisplayManager.h"

// --- الكائنات العامة ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
SensorManager sensorMgr;
DisplayManager displayMgr;

// --- متغيرات الإعدادات ---
struct SystemConfig {
  String ssid;
  String pass;
  String hostname;
} sysConfig;

// ================================================================
//  1. كود صفحة إدارة الملفات (Rescue File Manager)
// ================================================================
const char* fileManagerHTML = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>ESP32 Rescue Manager</title>
  <style>
    body { font-family: sans-serif; max-width: 600px; margin: auto; padding: 20px; background: #ffeaea; }
    .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); margin-bottom: 20px; border-top: 5px solid #dc3545; }
    h2 { margin-top: 0; color: #b02a37; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { text-align: left; padding: 10px; border-bottom: 1px solid #ddd; }
    button { cursor: pointer; padding: 5px 10px; border: none; border-radius: 4px; color: white; }
    .btn-del { background: #dc3545; }
    .btn-upload { background: #198754; padding: 10px; width: 100%; font-size: 16px; margin-top: 10px; }
    input[type=file] { margin-bottom: 10px; width: 100%; }
    a { text-decoration: none; color: #0d6efd; font-weight: bold; }
    .info { background: #fff3cd; padding: 10px; border-radius: 4px; margin-bottom: 15px; border: 1px solid #ffeeba; color: #856404; }
  </style>
</head>
<body>
  <h2>🆘 Rescue Mode</h2>
  <div class="info">
    You are in Rescue Mode.<br>
    Access Main Page: <a href="/">Click Here</a><br>
    Host: <b>http://esp32-rescue.local/manager</b>
  </div>
  <div class="card">
    <h3>Upload File (index.html / config.json)</h3>
    <form method="POST" action="/upload" enctype="multipart/form-data">
      <input type="file" name="data">
      <button class="btn-upload" type="submit">Upload to LittleFS</button>
    </form>
  </div>
  <div class="card">
    <h3>Current Files</h3>
    <div id="list">Loading...</div>
  </div>
  <script>
    function load() {
      fetch('/api/files').then(r => r.json()).then(files => {
        let h = '<table><tr><th>Name</th><th>Size</th><th>Action</th></tr>';
        files.forEach(f => {
          h += `<tr><td><a href="${f.name}" target="_blank">${f.name}</a></td><td>${f.size} B</td><td><button class="btn-del" onclick="del('${f.name}')">Delete</button></td></tr>`;
        });
        document.getElementById('list').innerHTML = h + '</table>';
      });
    }
    function del(n) { if(confirm('Delete '+n+'?')) fetch('/api/delete?file='+n, {method:'DELETE'}).then(load); }
    load();
  </script>
</body>
</html>
)rawliteral";

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    request->_tempFile = LittleFS.open(filename, "w");
  }
  if (request->_tempFile) request->_tempFile.write(data, len);
  if (final) {
    if (request->_tempFile) request->_tempFile.close();
  }
}

// ================================================================
//  2. المنطق (Logic)
// ================================================================

void loadConfig() {
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("No config file! Using Rescue Defaults.");
    sysConfig.ssid = ""; 
    sysConfig.pass = "";
    // هذا الاسم سيتم استخدامه إذا لم يوجد ملف إعدادات
    sysConfig.hostname = "esp32-rescue"; 
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) { 
    Serial.println("Config JSON Error"); 
    sysConfig.hostname = "esp32-rescue"; // اسم الطوارئ في حال تلف الملف
    return; 
  }

  sysConfig.ssid = doc["system"]["wifi_ssid"].as<String>();
  sysConfig.pass = doc["system"]["wifi_pass"].as<String>();
  // استخدام الاسم من الملف، أو الاسم الاحتياطي إذا كان الحقل فارغاً
  sysConfig.hostname = doc["system"]["hostname"] | "esp32-rescue"; 

  displayMgr.init(doc["displays"]);
  sensorMgr.init(doc["sensors"]);
}

void saveConfig(JsonVariant json) {
  File file = LittleFS.open("/config.json", "w");
  serializeJson(json, file);
  file.close();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) client->text(sensorMgr.getJson());
}

// ================================================================
//  3. Setup & Loop
// ================================================================

void setup() {
  Serial.begin(115200);
  
  if (!LittleFS.begin(true)) Serial.println("LittleFS Failed");
  
  loadConfig();

  // إعداد الواي فاي
  WiFi.mode(WIFI_STA);
  // ملاحظة: setHostname يضبط الاسم في الروتر (DHCP)
  WiFi.setHostname(sysConfig.hostname.c_str());
  
  if (sysConfig.ssid.length() > 0) {
    WiFi.begin(sysConfig.ssid.c_str(), sysConfig.pass.c_str());
    Serial.print("Connecting to WiFi: " + sysConfig.ssid);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nConnected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("No WiFi Credentials!");
  }

  // --- 2. تفعيل mDNS (الدومين الثابت) ---
  // سيحاول استخدام الاسم من الإعدادات، وإذا فشل يستخدم esp32-rescue
  String dnsName = sysConfig.hostname;
  if (dnsName == "") dnsName = "esp32-rescue";
  
  if (MDNS.begin(dnsName.c_str())) {
    Serial.println("mDNS responder started");
    Serial.println("Access via: http://" + dnsName + ".local/");
    Serial.println("Rescue via: http://" + dnsName + ".local/manager");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  // Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(LittleFS, "/index.html", "text/html"); });
  server.on("/manager", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", fileManagerHTML); });
  
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(LittleFS, "/config.json", "application/json"); });
  AsyncCallbackJsonWebHandler *saveHandler = new AsyncCallbackJsonWebHandler("/api/config", [](AsyncWebServerRequest *request, JsonVariant json) {
    saveConfig(json);
    request->send(200, "application/json", "{\"status\":\"saved\"}");
    delay(1000); ESP.restart();
  });
  server.addHandler(saveHandler);

  // File Manager API
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "[";
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while(file){
      if (json != "[") json += ",";
      json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
      file = root.openNextFile();
    }
    json += "]";
    request->send(200, "application/json", json);
  });
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){ request->send(200, "text/html", "<script>alert('Uploaded!');window.location.href='/manager';</script>"); }, handleUpload);
  server.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request){
     if(request->hasParam("file") && LittleFS.remove(request->getParam("file")->value())) request->send(200, "text/plain", "Deleted");
     else request->send(404);
  });

  // Test & WebSocket
  server.on("/api/test/adc", HTTP_GET, [](AsyncWebServerRequest *request){
     int pin = request->getParam("pin")->value().toInt();
     pinMode(pin, INPUT); request->send(200, "application/json", "{\"value\":" + String(analogRead(pin)) + "}");
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  ws.cleanupClients();
  sensorMgr.loop();
  
  static unsigned long lastWsSend = 0;
  if (millis() - lastWsSend > 1000) {
    lastWsSend = millis();
    if(ws.count() > 0) ws.textAll(sensorMgr.getJson());
  }
}