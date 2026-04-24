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
<!DOCTYPE html><html><head><meta charset="UTF-8">
<title>ChiperOS</title></head><body>
<h2>plusX Zero</h2>
<p>WiFi: ChiperOS | Pass: plusX123</p>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" name="file"><br>
<button type="submit">Upload</button>
</form>
</body></html>
)rawliteral";

void setup() {
  delay(3000);
  Serial.begin(115200);
  Serial.println("\n=== ChiperOS Boot ===");
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  
  // LittleFS
  Serial.println("1. Init LittleFS...");
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS failed!");
  } else {
    Serial.println("✅ LittleFS OK");
  }
  
  // PSRAM
  Serial.println("2. Check PSRAM...");
  if (psramInit()) {
    Serial.printf("✅ PSRAM: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("⚠️ No PSRAM");
  }
  
  // WiFi
  Serial.println("3. Init WiFi...");  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(ssid, pass)) {
    Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  } else {
    Serial.println("❌ WiFi AP failed!");
  }
  
  // Web Server
  Serial.println("4. Init Web Server...");
  server.on("/", []() {
    Serial.println("📄 Root page requested");
    server.send(200, "text/html", html);
  });
  
  server.on("/upload", HTTP_POST, 
    []() {
      Serial.println("📤 Upload complete");
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    },
    handleUpload
  );
  
  server.onNotFound([]() {
    Serial.println("❓ 404 Not Found");
    server.send(404, "text/plain", "Not Found");
  });
  
  server.begin();
  Serial.println("✅ Server started on port 80");
  Serial.printf("Final free heap: %d\n", ESP.getFreeHeap());
  Serial.println("=== Ready ===\n");
}

void loop() {
  server.handleClient();
}

void handleUpload() {
  static File f;
  HTTPUpload& up = server.upload();
  
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("Upload start: %s\n", up.filename.c_str());
    if (up.filename.endsWith(".bin")) {
      Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    } else {
      f = LittleFS.open("/" + up.filename, "w");
    }  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (up.filename.endsWith(".bin")) {
      Update.write(up.buf, up.currentSize);
    } else if (f) {
      f.write(up.buf, up.currentSize);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (up.filename.endsWith(".bin")) {
      if (Update.end(true)) {
        Serial.println("OTA OK, rebooting...");
        ESP.restart();
      }
    } else if (f) {
      f.close();
    }
  }
}
