#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>

WebServer server(80);

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

void handleUpload();

const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>ChiperOS</title></head>
<body style="background:#0d1117;color:#58a6ff;font-family:monospace;text-align:center;padding:20px;">
<h2 style="color:#7ee787;">plusX Zero</h2>
<p>WiFi: ChiperOS<br>Password: plusX123<br>IP: 192.100.0.1</p>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" name="file" accept=".bin,.json,.js"><br><br>
<button type="submit" style="padding:10px 20px;background:#238636;color:#fff;border:none;border-radius:5px;cursor:pointer;">Upload File</button>
</form>
</body></html>
)rawliteral";

void setup() {
  delay(3000);
  Serial.begin(115200);
  Serial.println("\n=== ChiperOS Boot ===");
  
  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS format");
  }
  
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());

  // Обработка главной страницы
  server.on("/", HTTP_GET, []() {
    Serial.println("📄 GET /");
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    delay(1);
  });

  // Обработка загрузки
  server.on("/upload", HTTP_POST, 
    []() {      Serial.println("📤 Upload done");
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "OK. Rebooting...");
      delay(1000);
      ESP.restart();
    },
    handleUpload
  );

  // Обработка 404
  server.onNotFound([]() {
    Serial.println("❓ 404");
    server.sendHeader("Connection", "close");
    server.send(404, "text/plain", "Not Found");
  });

  server.begin();
  Serial.println("✅ Server ready");
}

void loop() {
  server.handleClient();
  yield(); // Важно для стабильности
}

void handleUpload() {
  static File f;
  HTTPUpload& up = server.upload();
  
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("Upload: %s\n", up.filename.c_str());
    if (up.filename.endsWith(".bin")) {
      Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    } else {
      f = LittleFS.open("/" + up.filename, "w");
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (up.filename.endsWith(".bin")) {
      Update.write(up.buf, up.currentSize);
    } else if (f) {
      f.write(up.buf, up.currentSize);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (up.filename.endsWith(".bin")) {
      if (Update.end(true)) {
        Serial.println("OTA OK");
        ESP.restart();
      }
    } else if (f) {
      f.close();    }
  }
}
