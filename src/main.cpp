#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <SPI.h>

// Убрали TFT_eSPI пока - добавим позже
// #include <TFT_eSPI.h>
// TFT_eSPI tft = TFT_eSPI();

// === PIN DEFINITIONS ===
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   16
#define TFT_BL    48
#define TFT_SCK   17
#define TFT_MOSI  18
#define BTN1      45
#define BTN2      46
#define LED_PIN   47

WebServer server(80);

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

bool isFlashing = false;
unsigned long flashStartTime = 0;

void handleUpload();
void handleFileList();
void handleDeleteFile();

const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<title>ChiperOS</title><style>
body{background:#0d1117;color:#58a6ff;font-family:monospace;padding:20px;text-align:center;}
h1{color:#7ee787;} .card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin:10px 0;}
.btn{padding:8px 15px;border:none;border-radius:5px;cursor:pointer;margin:5px;background:#238636;color:#fff;}
.file-item{background:#0d1117;padding:10px;margin:5px 0;border-radius:5px;display:flex;justify-content:space-between;}
select,input{background:#0d1117;color:#fff;border:1px solid #30363d;padding:8px;border-radius:5px;margin:5px;width:100%;}
.address-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:10px 0;}
.address-option{background:#0d1117;border:1px solid #30363d;padding:10px;border-radius:5px;cursor:pointer;text-align:center;}
.address-option.selected{border-color:#238636;background:#1a2f1a;}
#log{background:#0d1117;padding:10px;border-radius:5px;margin-top:10px;font-size:12px;}
</style></head><body>
<h1>🛡️ ChiperOS</h1>
<div class="card"><h2>📂 Files</h2><div id="fileList">Loading...</div></div><div class="card">
<h2>🔥 Flash</h2>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin,.json,.js" required>
<div class="address-grid">
<div class="address-option selected" onclick="sel('0x0')" id="a0"><b>0x0</b><br><small>Boot</small></div>
<div class="address-option" onclick="sel('0x8000')" id="a1"><b>0x8000</b><br><small>Part</small></div>
<div class="address-option" onclick="sel('0x10000')" id="a2"><b>0x10000</b><br><small>App</small></div>
<div class="address-option" onclick="sel('0x300000')" id="a3"><b>0x300000</b><br><small>FS</small></div>
</div>
<input type="hidden" id="addr" name="address" value="0x10000">
<button type="submit" class="btn" style="width:100%">📤 Flash</button>
</form>
</div>
<div class="card"><h2>ℹ️ Info</h2><div id="info">Loading...</div></div>
<div id="log">Ready</div>
<script>
let addr='0x10000';
function sel(a){addr=a;document.querySelectorAll('.address-option').forEach(e=>e.classList.remove('selected'));document.getElementById('a'+(a=='0x0'?0:a=='0x8000'?1:a=='0x10000'?2:3)).classList.add('selected');document.getElementById('addr').value=a;}
function load(){fetch('/api/files').then(r=>r.json()).then(d=>{let h='';d.files.forEach(f=>h+='<div class="file-item"><span>📄 '+f.name+' ('+f.size+'B)</span><button class="btn" style="background:#da3633" onclick="del(\''+f.name+'\')">🗑</button></div>');document.getElementById('fileList').innerHTML=h||'Empty';});fetch('/api/info').then(r=>r.json()).then(d=>{document.getElementById('info').innerHTML='Heap: '+d.heap+'B<br>Uptime: '+d.uptime+'s';});}
function del(n){if(confirm('Delete '+n+'?')){fetch('/api/del?file='+n).then(()=>load());}}
document.querySelector('form').onsubmit=()=>document.getElementById('log').innerText='Uploading...';
setInterval(load,3000);load();
</script></body></html>
)rawliteral";

void setup() {
  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  
  Serial.begin(115200);
  Serial.println("\n=== STEP 1: Serial OK ===");
  digitalWrite(LED_PIN, LOW);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
  
  // Buttons
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  Serial.println("=== STEP 2: Buttons OK ===");
  digitalWrite(LED_PIN, LOW);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
  
  // LittleFS
  Serial.println("=== STEP 3: Init LittleFS ===");
  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS format");  } else {
    Serial.println("✅ LittleFS OK");
  }
  digitalWrite(LED_PIN, LOW);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
  
  // WiFi
  Serial.println("=== STEP 4: Init WiFi ===");
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(ssid, pass)) {
    Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  } else {
    Serial.println("❌ WiFi FAILED!");
  }
  digitalWrite(LED_PIN, LOW);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
  
  // Server
  Serial.println("=== STEP 5: Init Server ===");
  server.on("/", HTTP_GET, []() {
    Serial.println("📄 GET /");
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    delay(1);
  });
  
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/info", HTTP_GET, []() {
    String json = "{\"heap\":" + String(ESP.getFreeHeap()) + 
                  ",\"uptime\":" + String(millis()/1000) + "}";
    server.send(200, "application/json", json);
  });
  server.on("/api/del", HTTP_GET, handleDeleteFile);
  
  server.on("/upload", HTTP_POST, 
    []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "OK");
      digitalWrite(LED_PIN, HIGH);
      delay(1000);
      ESP.restart();
    },
    handleUpload
  );
  
  server.begin();
  Serial.println("=== STEP 6: Server OK ===");
  Serial.println("✅ READY! Connect to ChiperOS WiFi");  digitalWrite(LED_PIN, LOW);
}

void loop() {
  server.handleClient();
  
  // LED blink if flashing
  if (isFlashing) {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 200) {
      lastBlink = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    if (millis() - flashStartTime > 7000) {
      isFlashing = false;
      digitalWrite(LED_PIN, LOW);
    }
  }
  
  yield();
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
        Serial.println("✅ OTA OK");
        isFlashing = true;
        flashStartTime = millis();
      }
    } else if (f) {
      f.close();
    }
  }}

void handleFileList() {
  File root = LittleFS.open("/");
  String json = "{\"files\":[";
  bool first = true;
  File file = root.openNextFile();
  while (file) {
    if (!first) json += ",";
    json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
    first = false;
    file = root.openNextFile();
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleDeleteFile() {
  String file = server.arg("file");
  if (LittleFS.remove(file)) {
    Serial.println("🗑 Deleted: " + file);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(500, "text/plain", "Delete failed");
  }
}
