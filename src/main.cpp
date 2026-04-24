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
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS</title><style>
body{background:#0d1117;color:#58a6ff;font-family:monospace;text-align:center;padding:20px;}
h2{color:#7ee787;} button{padding:10px 20px;background:#238636;color:#fff;border:none;border-radius:5px;margin:10px;}
</style></head><body>
<h2>plusX Zero</h2>
<p>WiFi: ChiperOS<br>Pass: plusX123<br>IP: 192.100.0.1</p>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin,.json,.js"><br>
<button type="submit">Upload</button>
</form>
</body></html>
)rawliteral";

void setup() {
  delay(3000); // Ждем стабильности USB
  Serial.begin(115200);
  Serial.println("\n🚀 ChiperOS Boot...");
  
  // БЕЗ дисплея для теста!
  
  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS format");
  }

  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("📡 AP: %s | IP: %s\n", ssid, ip.toString().c_str());

  server.on("/", []() { server.send(200, "text/html", html); });
  server.on("/upload", HTTP_POST, 
    []() { server.send(200, "text/plain", "✅ Done"); delay(1000); ESP.restart(); }, 
    handleUpload
  );
  
  server.begin();
  Serial.println("✅ Ready!");
}

void loop() {
  server.handleClient();
}

void handleUpload() {
  HTTPUpload& up = server.upload();
  static File f;
  
  if (up.status == UPLOAD_FILE_START) {
    if (up.filename.endsWith(".bin")) {
      Serial.println("🔥 Flashing...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
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
      if (Update.end(true)) { Serial.println("🔄 OTA OK"); ESP.restart(); }
    } else if (f) {
      f.close();
    }
  }
}
