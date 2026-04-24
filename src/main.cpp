#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <esp_random.h>

// === PIN DEFINITIONS ===
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   16
#define TFT_BL    48
#define TFT_SCK   17
#define TFT_MOSI  18
#define BTN1      45  // Touch button 1
#define BTN2      46  // Touch button 2
#define LED_PIN   47  // Status LED

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

bool isFlashing = false;
unsigned long flashStartTime = 0;

// Forward declarations
void handleUpload();
void handleFileList();
void handleDeleteFile();

// === WEB INTERFACE ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS</title><style>
*{margin:0;padding:0;box-sizing:border-box;}
body{background:#0d1117;color:#58a6ff;font-family:'Courier New',monospace;padding:20px;}
h1{color:#7ee787;text-align:center;margin-bottom:20px;font-size:24px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin-bottom:15px;}
h2{color:#58a6ff;font-size:16px;margin-bottom:10px;border-bottom:1px solid #30363d;padding-bottom:5px;}
.file-item{background:#0d1117;border:1px solid #21262d;border-radius:5px;padding:10px;margin:5px 0;display:flex;justify-content:space-between;align-items:center;}
.file-info{flex:1;}
.file-name{color:#7ee787;font-weight:bold;font-size:14px;}
.file-size{color:#8b949e;font-size:12px;}
.btn{padding:8px 15px;border:none;border-radius:5px;cursor:pointer;font-weight:bold;margin:2px;font-size:13px;}
.btn-primary{background:#238636;color:#fff;}.btn-danger{background:#da3633;color:#fff;}
.btn-warning{background:#9e6a03;color:#fff;}
.btn:hover{opacity:0.8;}
select,input[type="text"]{background:#0d1117;color:#58a6ff;border:1px solid #30363d;padding:8px;border-radius:5px;margin:5px 0;width:100%;}
.flash-section{background:#1a1f2e;border:2px solid #238636;border-radius:8px;padding:15px;margin:10px 0;}
.address-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:10px 0;}
.address-option{background:#0d1117;border:1px solid #30363d;padding:10px;border-radius:5px;cursor:pointer;}
.address-option.selected{border-color:#238636;background:#1a2f1a;}
.led-status{width:15px;height:15px;border-radius:50%;display:inline-block;margin-right:5px;}
.led-on{background:#3fb950;box-shadow:0 0 10px #3fb950;}
.led-off{background:#484f58;}
#log{background:#0d1117;border:1px solid #30363d;padding:10px;border-radius:5px;margin-top:10px;font-size:12px;color:#8b949e;min-height:40px;}
</style></head><body>
<h1>🛡️ ChiperOS Control</h1>

<div class="card">
  <h2>📂 File Manager</h2>
  <div id="fileList">Loading...</div>
</div>

<div class="card flash-section">
  <h2>🔥 Flash Firmware</h2>
  <form id="uploadForm" method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" id="fileInput" name="firmware" accept=".bin,.json,.js,.txt,.cfg" required>
    <div style="margin:10px 0;">
      <label style="color:#8b949e;font-size:12px;">Flash Address:</label>
      <div class="address-grid">
        <div class="address-option selected" onclick="selectAddress('0x0000')" id="addr-0x0000">
          <strong>0x0000</strong><br><small>Bootloader</small>
        </div>
        <div class="address-option" onclick="selectAddress('0x8000')" id="addr-0x8000">
          <strong>0x8000</strong><br><small>Partition Table</small>
        </div>
        <div class="address-option" onclick="selectAddress('0x10000')" id="addr-0x10000">
          <strong>0x10000</strong><br><small>Application</small>
        </div>
        <div class="address-option" onclick="selectAddress('0x300000')" id="addr-0x300000">
          <strong>0x300000</strong><br><small>LittleFS</small>
        </div>
      </div>
      <input type="hidden" id="selectedAddress" name="address" value="0x10000">
    </div>
    <button type="submit" class="btn btn-primary" style="width:100%;padding:12px;">📤 Flash to Selected Address</button>
  </form>
</div>

<div class="card">
  <h2>📊 System Info</h2>
  <div id="sysInfo">Loading...</div>
</div>
<div id="log">Ready</div>

<script>
let selectedAddr='0x10000';

function selectAddress(addr){
  selectedAddr=addr;
  document.querySelectorAll('.address-option').forEach(el=>el.classList.remove('selected'));
  document.getElementById('addr-'+addr).classList.add('selected');
  document.getElementById('selectedAddress').value=addr;
  log('Selected address: '+addr);
}

function log(msg){
  document.getElementById('log').innerHTML='⏱ '+new Date().toLocaleTimeString()+': '+msg;
}

function refreshFiles(){
  fetch('/api/files').then(r=>r.json()).then(data=>{
    let html='';
    data.files.forEach(f=>{
      html+='<div class="file-item"><div class="file-info"><div class="file-name">📄 '+f.name+'</div><div class="file-size">'+f.size+' bytes</div></div>';
      if(!f.name.endsWith('.bin')){
        html+='<button class="btn btn-warning" onclick="downloadFile(\''+f.name+'\')">⬇</button>';
      }
      html+='<button class="btn btn-danger" onclick="deleteFile(\''+f.name+'\')">🗑</button></div>';
    });
    document.getElementById('fileList').innerHTML=html||'<div style="color:#8b949e;text-align:center;padding:20px;">No files</div>';
  });
  
  fetch('/api/sysinfo').then(r=>r.json()).then(d=>{
    let ledClass=d.led?'led-on':'led-off';
    document.getElementById('sysInfo').innerHTML='<div><span class="led-status '+ledClass+'"></span>LED: '+(d.led?'ON':'OFF')+'</div>'+
      '<div>Free Heap: '+d.heap+' bytes</div>'+
      '<div>PSRAM: '+d.psram+' bytes</div>'+
      '<div>Uptime: '+d.uptime+'s</div>';
  });
}

function deleteFile(name){
  if(confirm('Delete '+name+'?')){
    fetch('/api/delete?file='+encodeURIComponent(name)).then(()=>{log('Deleted '+name);refreshFiles();});
  }
}

function downloadFile(name){
  window.location.href='/download?file='+encodeURIComponent(name);
  log('Downloading '+name);
}
document.getElementById('uploadForm').onsubmit=()=>{
  log('⏳ Uploading to '+selectedAddr+'...');
  document.querySelector('button[type="submit"]').disabled=true;
};

setInterval(refreshFiles,3000);
refreshFiles();
</script></body></html>
)rawliteral";

void setup() {
  // LED init
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Buttons init (touch)
  pinMode(BTN1, INPUT);
  pinMode(BTN2, INPUT);
  
  delay(3000);
  Serial.begin(115200);
  Serial.println("\n=== ChiperOS Boot ===");
  
  // Display init
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  
  // Splash screen
  tft.drawString("ChiperOS", 20, 20);
  tft.drawString("plusX Zero", 20, 50);
  tft.drawString("WiFi: ChiperOS", 20, 80);
  tft.drawString("Pass: plusX123", 20, 110);
  tft.drawString("IP: 192.100.0.1", 20, 140);
  tft.drawString("Initializing...", 20, 170);
  
  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }
  
  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS format");
    tft.drawString("Formatting FS...", 20, 200);
  }  
  // WiFi
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  
  tft.drawString("WiFi: OK", 20, 200);
  
  // Web Server
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    delay(1);
  });
  
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/delete", HTTP_GET, handleDeleteFile);
  server.on("/api/sysinfo", HTTP_GET, []() {
    String json = "{\"led\":" + String(digitalRead(LED_PIN)) + 
                  ",\"heap\":" + String(ESP.getFreeHeap()) +
                  ",\"psram\":" + String(ESP.getPsramSize()) +
                  ",\"uptime\":" + String(millis()/1000) + "}";
    server.send(200, "application/json", json);
  });
  
  server.on("/download", HTTP_GET, []() {
    String file = server.arg("file");
    File f = LittleFS.open(file, "r");
    if (f) {
      server.streamFile(f, "application/octet-stream");
      f.close();
    } else {
      server.send(404, "text/plain", "File not found");
    }
  });
  
  server.on("/upload", HTTP_POST, 
    []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "OK");
      
      // LED indication - solid for 7 seconds
      digitalWrite(LED_PIN, HIGH);
      flashStartTime = millis();
      isFlashing = true;
      
      delay(1000);
      ESP.restart();
    },
    handleUpload  );
  
  server.begin();
  Serial.println("✅ Server ready");
  
  tft.drawString("Server: OK", 20, 230);
  tft.drawString("READY", 20, 260);
}

void loop() {
  server.handleClient();
  
  // LED blink during flash
  if (isFlashing) {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 200) {
      lastBlink = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    
    // Check if 7 seconds passed
    if (millis() - flashStartTime > 7000) {
      isFlashing = false;
      digitalWrite(LED_PIN, LOW);
    }
  }
  
  // Touch buttons
  static unsigned long lastBtnCheck = 0;
  if (millis() - lastBtnCheck > 100) {
    lastBtnCheck = millis();
    
    if (digitalRead(BTN1) == LOW) {
      // Button 1 pressed
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("BTN1", 100, 20);
      delay(50);
    }
    if (digitalRead(BTN2) == LOW) {
      // Button 2 pressed
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString("BTN2", 100, 20);
      delay(50);
    }
  }
  
  yield();
}

void handleUpload() {  static File f;
  HTTPUpload& up = server.upload();
  
  // LED blink during upload
  static unsigned long lastLed = 0;
  if (millis() - lastLed > 300) {
    lastLed = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  if (up.status == UPLOAD_FILE_START) {
    String addr = server.arg("address");
    Serial.printf("Upload: %s to %s\n", up.filename.c_str(), addr.c_str());
    
    if (up.filename.endsWith(".bin")) {
      uint32_t address = strtoul(addr.c_str(), NULL, 0);
      Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      // Update.setAddress(address); // если нужно
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
        Serial.println("✅ OTA Success");
      }
    } else if (f) {
      f.close();
    }
  }
}

void handleFileList() {
  File root = LittleFS.open("/");
  String json = "{\"files\":[";
  bool first = true;
  
  File file = root.openNextFile();
  while (file) {
    if (!first) json += ",";
    json += "{\"name\":\"" + String(file.name()) + 
            "\",\"size\":" + String(file.size()) + "}";
    first = false;
    file = root.openNextFile();  }
  json += "]}";
  
  server.send(200, "application/json", json);
}

void handleDeleteFile() {
  String file = server.arg("file");
  if (LittleFS.remove(file)) {
    server.send(200, "text/plain", "OK");
    Serial.println("🗑 Deleted: " + file);
  } else {
    server.send(500, "text/plain", "Delete failed");
  }
}
