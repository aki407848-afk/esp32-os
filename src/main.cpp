// ==========================================
// 📌 RESERVED PINS (Future Modules)
// SD Card:   CS=10, MOSI=11, MISO=9, SCK=12
// NRF24L01:  CE=5,  CSN=4,  SCK=7, MOSI=6, MISO=8
// CC1101:    CSN=1, GDO0=3, GDO2=2
// IR Module: TX=38, RX=39
// Buttons:   UP=45, DOWN=46, OK=47
// Battery:   ADC=4, CHG=5
// ==========================================

// === DISPLAY PINS (ST7789 1.14" 135x240) ===
#define TFT_CS   14
#define TFT_DC   15
#define TFT_RST  16
#define TFT_BL   48
#define TFT_SCK  17
#define TFT_MOSI 18
#define TFT_MISO -1

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

// === FORWARD DECLARATIONS ===
void handleUpload();

// === SIMPLE WEB UI ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS Web</title><style>
body{background:#0d1117;color:#58a6ff;font-family:monospace;text-align:center;padding:20px;margin:0;}
h2{color:#7ee787;} input{margin:10px;padding:10px;border-radius:5px;border:1px solid #30363d;background:#161b22;color:#fff;}
button{padding:10px 20px;background:#238636;color:#fff;border:none;border-radius:5px;cursor:pointer;font-weight:bold;}
button:hover{background:#2ea043;} #log{margin-top:15px;color:#8b949e;font-size:14px;}
</style></head><body>
<h2>plusX Zero | Firmware Loader</h2>
<form method="POST" action="/upload" enctype="multipart/form-data">
  <input type="file" name="firmware" accept=".bin,.json,.js,.txt,.cfg"><br>
  <button type="submit">📤 Flash / Save File</button></form>
<div id="log">Ready for firmware via web...</div>
<script>
document.querySelector('form').onsubmit=()=>{document.getElementById('log').innerText='⏳ Uploading...';};
</script></body></html>
)rawliteral";

void setup() {
  delay(1500);
  Serial.begin(115200);

  // 🖥️ Display Init
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  
  tft.drawString("wifi: ChiperOS",        20, 20);
  tft.drawString("device: plusX Zero",    20, 50);
  tft.drawString("Web: 192.100.0.1",      20, 80);
  tft.drawString("pass: plusX123",        20, 110);
  tft.drawString("Ready for firmware",    20, 140);
  tft.drawString("via web interface...",  20, 170);
  
  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  // 💾 File System
  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS format");
  }

  // 📡 WiFi AP
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("📡 AP: %s | IP: %s\n", ssid, ip.toString().c_str());

  // 🌐 Web Server
  server.on("/", []() { 
    server.send(200, "text/html", html); 
  });
  
  server.on("/upload", HTTP_POST, 
    []() { 
      server.send(200, "text/plain", "✅ Done. Rebooting..."); 
      delay(1000);      ESP.restart();
    }, 
    handleUpload  // ← Теперь функция объявлена выше!
  );
  
  server.begin();
  Serial.println("✅ System Ready.");
}

void loop() {
  server.handleClient();
}

// === FILE / OTA UPLOAD HANDLER ===
void handleUpload() {
  HTTPUpload& up = server.upload();
  static File f;
  
  if (up.status == UPLOAD_FILE_START) {
    String name = "/" + up.filename;
    if (up.filename.endsWith(".bin")) {
      Serial.print("🔥 Flashing: "); 
      Serial.println(up.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        Update.printError(Serial);
      }
    } else {
      Serial.print("💾 Saving: "); 
      Serial.println(up.filename);
      f = LittleFS.open(name, "w");
    }
  } 
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (up.filename.endsWith(".bin")) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        Update.printError(Serial);
      }
    } else {
      if (f) {
        f.write(up.buf, up.currentSize);
      }
    }
  } 
  else if (up.status == UPLOAD_FILE_END) {
    if (up.filename.endsWith(".bin")) {
      if (Update.end(true)) { 
        Serial.println("🔄 OTA Success"); 
        ESP.restart(); 
      }
    } else {      if (f) {
        f.close();
      }
    }
  }
}
