#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <sys/time.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>

#ifndef WHITE
#define WHITE SSD1306_WHITE
#define BLACK SSD1306_BLACK
#endif

#ifndef HID_MOUSE
#define HID_MOUSE 0x03C2
#endif

// ─── 1. ОБЪЯВЛЕНИЯ ТИПОВ ──────────────────────────────────────────────────────
enum AppMode : byte { M_TIME, M_CALC, M_RACER, M_WIFI, M_TETRIS, M_BLOCKS, M_SNAKE, M_BLE, M_ADXL, M_DOOM, M_NOTES, M_MATH };
enum AppWifiState : byte { W_START_SCAN, W_SCANNING, W_LIST, W_KEYBOARD, W_CONNECTING, W_CONNECTED, W_FAILED };
enum BlockState : byte { B_GENERATE, B_SELECT, B_PLACE, B_DESTROY, B_GAMEOVER };

struct WifiNet { String ssid; int32_t rssi; };
struct BlockShape { byte w, h; uint16_t mask; };
struct Car { float y; float v; byte x; };
struct SnakePt { int8_t x, y; };

struct Player { double x, y; double dir_x, dir_y; double plane_x, plane_y; double velocity; int health; int ammo; };
struct Entity { uint16_t uid; uint8_t type; double x, y; uint8_t state; int health; double distance; int timer; };

#define MAX_NOTES 5
enum NoteState { N_LIST, N_ACTION, N_EDIT_TITLE, N_EDIT_TEXT, N_DEL, N_LOOK };
struct Note { char title[12]; char text[32]; bool isTicked; bool inUse; };

enum MathState { MG_PLAY, MG_OVER, MG_WIN };
enum AdxlState { ADXL_MENU, ADXL_CUBE, ADXL_BALLS };
struct Ball { float x, y; float vx, vy; float radius; float mass; };

// ─── 2. ПРОТОТИПЫ ФУНКЦИЙ ────────────────────────────────────────────────────
void drawMenu(int ox); void drawTime(int ox); void drawCalc(int ox); void drawRacer(int ox); 
void drawWifi(int ox); void drawTetris(int ox); void drawBlocks(int ox); 
void drawSnake(int ox); void drawBLE(int ox); void drawADXL(int ox);
void drawNotes(int ox); void drawMath(int ox);
void loopMenu(); void loopTime(); void loopCalc(); void loopRacer(); void loopWifi();
void loopTetris(); void loopBlocks(); void loopSnake(); void loopBLE(); void loopADXL(); 
void loopDoom(); void runDoomFrame(); void doomInit(); void notesInit(); void mathInit(); void mathGenerate();
void loopNotes(); void loopMath(); void saveNotes();
void printCenter(const String& text, int x, int y, int bw);
void printRight(const String& text, int x, int y);
void handleRoot(); void handleSetTime();

// ─── 3. ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПИНЫ ────────────────────────────────────────────
Adafruit_SSD1306 display(128, 64, &Wire, -1);
uint8_t* display_buf; 
WebServer server(80);

bool isBleStarted = false;
bool isWifiApStarted = false;

const byte PINS[5] = {0, 1, 3, 4, 5}; 
AppMode activeApp = M_TIME;
byte mSel = 0;
const byte NUM_APPS = 12;
const char* const APP_NAMES[12] = {"TIME", "CALC", "RACER", "WI-FI", "TETRIS", "BLAST", "SNAKE", "MOUSE", "ADXL", "DOOM", "NOTES", "MATH"};

// ─── ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ПРИЛОЖЕНИЙ ────────────────────────────────────────
Note notes[MAX_NOTES];
NoteState nSt = N_LIST;
int8_t nSel = 0, nActSel = 0;
char nKbdBuf[32] = "";
int8_t nKbdX = 0, nKbdY = 0;
float nScrollY = 0, targetNScrollY = 0, nActAnimY = 0, nLookScrollY = 0;
float kbdAnimX = 0, kbdAnimY = 0, nSelAnimY = 0; 

const char N_KBD_LAYOUT[8][5] = {
  {'a','b','c','d','e'}, {'f','g','h','i','j'}, {'k','l','m','n','o'}, {'p','q','r','s','t'},
  {'u','v','w','x','y'}, {'z','0','1','2','3'}, {'4','5','6','7','8'}, {'9','_','<','V','X'}
};

MathState mgSt = MG_PLAY;
String mgOpStr = "";
int mgAns, mgOpts[4];
int8_t mgSel = 0;
int mgLevel = 1;
uint32_t mgStartTime = 0;
float mgSelAnimY = 0;

// --- НАСТРОЙКИ ADXL BALLS ---
#define ADXL345_ADDR 0x53 
#define SWAP_X_Y     false   
#define INVERT_X     true   
#define INVERT_Y     true  

const float ACCEL_SCALE_X = 0.0030f; 
const float ACCEL_SCALE_Y = 0.0045f; 
const float BASE_RESTITUTION = 0.70f;
const float FRICTION = 0.985f;       

AdxlState adxlSt = ADXL_MENU;
int8_t adxlSel = 0;
float adxlSelAnimY = 0;
float shakeEnergy = 0.0f;
int16_t prev_raw_x = 0, prev_raw_y = 0;
const int NUM_BALLS = 5;
Ball balls[NUM_BALLS] = {
  { 30.0, 20.0, 0.0, 0.0, 4.0, 16.0 }, { 40.0, 20.0, 0.0, 0.0, 5.0, 25.0 },
  { 50.0, 20.0, 0.0, 0.0, 6.0, 36.0 }, { 30.0, 40.0, 0.0, 0.0, 7.0, 49.0 },
  { 40.0, 45.0, 0.0, 0.0, 5.0, 25.0 }
};

int hiRacer=0, hiTetris=0, hiBlast=0, hiSnake=0, hiDoom=0, hiMath=0;

void loadScoresAndNotes() {
  byte magic;
  EEPROM.get(500, magic);
  if (magic != 0x47) { 
    hiRacer=0; hiTetris=0; hiBlast=0; hiSnake=0; hiDoom=0; hiMath=0;
    EEPROM.put(0, hiRacer); EEPROM.put(4, hiTetris); EEPROM.put(8, hiBlast);
    EEPROM.put(12, hiSnake); EEPROM.put(16, hiDoom); EEPROM.put(20, hiMath);
    memset(notes, 0, sizeof(notes));
    for(int i=0; i<MAX_NOTES; i++) EEPROM.put(30 + i * sizeof(Note), notes[i]);
    magic = 0x47; EEPROM.put(500, magic); EEPROM.commit();
  } else {
    EEPROM.get(0, hiRacer); EEPROM.get(4, hiTetris); EEPROM.get(8, hiBlast);
    EEPROM.get(12, hiSnake); EEPROM.get(16, hiDoom); EEPROM.get(20, hiMath);
    for(int i=0; i<MAX_NOTES; i++) EEPROM.get(30 + i * sizeof(Note), notes[i]);
  }
}

void saveScore(int addr, int &hi, int score) {
  if (score > hi) { hi = score; EEPROM.put(addr, hi); EEPROM.commit(); }
}

void saveNotes() {
  for(int i=0; i<MAX_NOTES; i++) EEPROM.put(30 + i * sizeof(Note), notes[i]);
  EEPROM.commit();
}

// ─── 4. BLUETOOTH MOUSE И АКСЕЛЕРОМЕТР ───────────────────────────────────────
BLEServer* pServer = nullptr; BLEHIDDevice* hid = nullptr; BLECharacteristic* input = nullptr;
bool isBleConnected = false; uint8_t lastMouseButtons = 0; bool mouseWasMoving = false; unsigned long lastBleTick = 0;

const int DEADZONE = 4; 
const int SENSITIVITY = 2; 

const uint8_t hidReportMap[] = { 
  0x05,0x01,0x09,0x02,0xA1,0x01,0x85,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,
  0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,
  0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,
  0x95,0x03,0x81,0x06,0xC0,0xC0 
};

class BLECallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override { isBleConnected = true; }
  void onDisconnect(BLEServer* pServer) override { isBleConnected = false; BLEDevice::startAdvertising(); }
};
void bleInit() {
  BLEDevice::init("Trxn MOUSE");
  pServer = BLEDevice::createServer(); pServer->setCallbacks(new BLECallbacks());
  hid = new BLEHIDDevice(pServer); input = hid->inputReport(1); 
  hid->manufacturer()->setValue("Trxn OS"); hid->pnp(0x02, 0xe502, 0xa111, 0x0210); hid->hidInfo(0x00, 0x01); hid->setBatteryLevel(100);
  BLESecurity *pSec = new BLESecurity(); pSec->setAuthenticationMode(ESP_LE_AUTH_BOND); pSec->setCapability(ESP_IO_CAP_NONE); pSec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  hid->reportMap((uint8_t*)hidReportMap, sizeof(hidReportMap)); hid->startServices();
  BLEAdvertising *pAdv = pServer->getAdvertising(); pAdv->setAppearance(HID_MOUSE); pAdv->addServiceUUID(hid->hidService()->getUUID()); BLEDevice::startAdvertising();
}

int16_t adxlX=0, adxlY=0, adxlZ=0;
void adxlInit() { 
  Wire.beginTransmission(0x53); Wire.write(0x2D); Wire.write(0x08); Wire.endTransmission(); 
  Wire.beginTransmission(0x53); Wire.write(0x31); Wire.write(0x0B); Wire.endTransmission(); 
}
void readADXL() {
  Wire.beginTransmission(0x53); Wire.write(0x32); Wire.endTransmission(false);
  Wire.requestFrom((uint16_t)0x53, (uint8_t)6, true);
  if(Wire.available() >= 6) { adxlX=Wire.read()|(Wire.read()<<8); adxlY=Wire.read()|(Wire.read()<<8); adxlZ=Wire.read()|(Wire.read()<<8); }
}

bool btnState[5]={false}, btnJustPressed[5]={false}, btnJustReleased[5]={false};
unsigned long btnPressTime[5]={0}, bMs[5]={0};
bool selLongFired = false, selLongTriggeredNow = false;

void updateButtons() {
  selLongTriggeredNow = false;
  for (byte i = 0; i < 5; i++) {
    btnJustPressed[i] = false; btnJustReleased[i] = false;
    bool raw = (digitalRead(PINS[i]) == LOW); 
    if (raw != btnState[i]) {
      if (millis() - bMs[i] > 30) { 
        btnState[i] = raw; bMs[i] = millis();
        if (raw) { btnJustPressed[i] = true; btnPressTime[i] = millis(); if (i == 4) selLongFired = false; } 
        else { btnJustReleased[i] = true; }
      }
    }
    if (i == 4 && btnState[4] && !selLongFired && (millis() - btnPressTime[4] >= 600)) { selLongFired = true; selLongTriggeredNow = true; }
  }
}
bool bp(byte i) { if (i == 4) return (btnJustReleased[4] && !selLongFired); return btnJustPressed[i]; }

int hr = 12, mn = 0, sc = 0;
bool wifiTimeSynced = false; 
unsigned long lastSecTick = 0;

void updateClock() {
  // Выполняем проверку и обновление строго 1 раз в секунду
  if (millis() - lastSecTick >= 1000) {
    lastSecTick = millis();
    
    // Запрашиваем время у системы ТОЛЬКО если есть синхронизация по NTP
    if (wifiTimeSynced) {
      struct tm timeinfo;
      // Неблокирующий вызов (таймаут 0 мс)
      if (getLocalTime(&timeinfo, 0)) { 
        hr = timeinfo.tm_hour; 
        mn = timeinfo.tm_min; 
        sc = timeinfo.tm_sec; 
        return; // Успешно обновили время из системы, выходим
      }
    }
    
    // Если интернета нет или getLocalTime не успел ответить, тикаем локально
    sc++;
    if (sc >= 60) { sc = 0; mn++; }
    if (mn >= 60) { mn = 0; hr++; }
    if (hr >= 24) { hr = 0; }
  }
}

float cameraX = 0, targetCameraX = 0, menuScrollY = 0, targetMenuScrollY = 0, menuBoxY = 0, targetMenuBoxY = 0; 
float calcX = 3, calcY = 36, targetCalcX = 3, targetCalcY = 36, wifiSY = 0, targetWifiSY = 0, vkX = 5, vkY = 40, targetVkX = 5, targetVkY = 40;

float ease(float curr, float target, float spd) { 
  float diff = target - curr; 
  if (abs(diff) < 0.2) return target; 
  return curr + diff * spd; 
}

void updateAnimations() {
  cameraX = ease(cameraX, targetCameraX, 0.4);
  menuScrollY = ease(menuScrollY, targetMenuScrollY, 0.4);
  menuBoxY = ease(menuBoxY, targetMenuBoxY, 0.45); 
  calcX = ease(calcX, targetCalcX, 0.45); calcY = ease(calcY, targetCalcY, 0.45);
  wifiSY = ease(wifiSY, targetWifiSY, 0.4); vkX = ease(vkX, targetVkX, 0.45); vkY = ease(vkY, targetVkY, 0.45);
}

void printRight(const String& text, int x, int y) { int16_t x1, y1; uint16_t w, h; display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h); display.setCursor(x - w, y); display.print(text); }
void printCenter(const String& text, int x, int y, int bw) { int16_t x1, y1; uint16_t w, h; display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h); display.setCursor(x + (bw - w) / 2, y); display.print(text); }

// ─── КАЛЬКУЛЯТОР ─────────────────────────────────────────────────────────────
const char KEYS_NORM[4][4] = { {'7','8','9','/'}, {'4','5','6','*'}, {'1','2','3','-'}, {'C','0','=','+'} };
const char KEYS_ENG[5][4]  = { {'R','^','S','T'}, {'7','8','9','/'}, {'4','5','6','*'}, {'1','2','3','-'}, {'C','0','=','+'} };
char dv[16] = "0", op1[16] = ""; char oper = 0; bool isNew = true; byte cRow = 0, cCol = 0, cMaxRows = 4; bool cEng = false;
void cClear() { strcpy(dv, "0"); op1[0] = 0; oper = 0; isNew = true; cRow = 0; cCol = 0; }
void cFormat(float r) { if(isnan(r)) strcpy(dv, "ERR"); else if(r==(long)r) ltoa((long)r, dv, 10); else dtostrf(r, 0, 3, dv); isNew = true; }
void cCalc() { 
  if(!oper)return; 
  float n1=atof(op1), n2=atof(dv), r=0; 
  if(oper=='+')r=n1+n2; else if(oper=='-')r=n1-n2; else if(oper=='*')r=n1*n2; 
  else if(oper=='/'){ if(n2==0){strcpy(dv,"ERR"); oper=0; isNew=true; return;} r=n1/n2; } 
  cFormat(r); oper=0; 
}
void cKey(char k) {
  if(k>='0'&&k<='9'){ if(isNew||!strcmp(dv,"0")||!strcmp(dv,"ERR")){dv[0]=k;dv[1]=0;isNew=false;} else if(strlen(dv)<12){byte l=strlen(dv);dv[l]=k;dv[l+1]=0;} } 
  else if(k=='C'){ cClear(); if(cEng){ cEng=false; cMaxRows=4; } } 
  else if(k=='+'||k=='-'||k=='*'||k=='/'){ if(oper&&!isNew)cCalc(); strcpy(op1, dv); oper=k; isNew=true; } 
  else if(k=='='){ if(!cEng&&!strcmp(dv,"1")&&oper==0&&!isNew){cEng=true;cMaxRows=5;cClear();return;} if(oper&&!isNew)cCalc(); } 
  else if(k=='R'){ float v=atof(dv); if(v>=0)cFormat(sqrt(v)); else strcpy(dv,"ERR"); } 
  else if(k=='^'){ float v=atof(dv); cFormat(v*v); } else if(k=='S'){ cFormat(sin(atof(dv))); } else if(k=='T'){ cFormat(cos(atof(dv))); }
}

float rX = 28; int rScore = 0; bool rGameOver = false; Car rObs[3];
void racerInit() { 
  rX = 28; rScore = 0; rGameOver = false; 
  rObs[0].y = -30; rObs[0].x = random(10, 44); rObs[0].v = random(12, 20)/10.0; 
  rObs[1].y = -80; rObs[1].x = random(10, 44); rObs[1].v = random(12, 20)/10.0; 
  rObs[2].y = -130; rObs[2].x = random(10, 44); rObs[2].v = random(12, 20)/10.0; 
}

AppWifiState wSt = W_START_SCAN; WifiNet wNet[12]; int wCt = 0; byte wSel = 0;
const char VK[4][3] = { {'1','2','3'}, {'4','5','6'}, {'7','8','9'}, {'<','0','>'} };
byte vkR = 0, vkC = 0; char wPwd[16] = ""; unsigned long wTm = 0; byte connStep = 0; 

uint16_t tBrd[20]; int8_t tX = 3, tY = 0; byte tTy = 0, tRot = 0; unsigned long tTk = 0; int tSc = 0; bool tDead = false;
const uint16_t TET[7][4] = { {0x0F00, 0x2222, 0x0F00, 0x2222}, {0x44C0, 0x8E00, 0x6440, 0x0E20}, {0x4460, 0x0E80, 0xC440, 0x2E00}, {0xCC00, 0xCC00, 0xCC00, 0xCC00}, {0x06C0, 0x8C40, 0x6C00, 0x4620}, {0x0E40, 0x4C40, 0x4E00, 0x4640}, {0x0C60, 0x4C80, 0xC600, 0x2640} };
bool tCol(int8_t nx, int8_t ny, byte nrot) { uint16_t p = TET[tTy][nrot]; for(int i=0; i<4; i++) for(int j=0; j<4; j++) if((p & (1 << (15 - (i*4+j)))) != 0) { int bx = nx+j, by = ny+i; if(bx<0 || bx>=10 || by>=20) return true; if(by>=0 && ((tBrd[by] & (1<<bx)) != 0)) return true; } return false; }
void tSpawn() { tX = 3; tY = 0; tTy = random(7); tRot = 0; if(tCol(tX, tY, tRot)) { tDead = true; saveScore(4, hiTetris, tSc); } }
void tetrisInit() { for(int i=0; i<20; i++) tBrd[i]=0; tSc=0; tDead=false; tSpawn(); tTk=millis(); }
void tMerge() { uint16_t p=TET[tTy][tRot]; for(int i=0; i<4; i++) for(int j=0; j<4; j++) if((p&(1<<(15-(i*4+j))))!=0 && tY+i>=0) tBrd[tY+i]|=(1<<(tX+j)); for(int r=19; r>=0; r--) if(tBrd[r]==0x03FF) { for(int j=r; j>0; j--) tBrd[j]=tBrd[j-1]; tBrd[0]=0; tSc+=10; r++; } tSpawn(); }

byte grid[8][8]; int8_t activeShapes[3]; byte selectedShapeIdx = 0; int8_t blockX = 3, blockY = 3; int bScore = 0; bool bGameOver = false; BlockState bState = B_GENERATE;
const BlockShape SHAPES[12] = { {1,1,1}, {2,1,3}, {1,2,9}, {3,1,7}, {1,3,73}, {2,2,27}, {3,3,511}, {2,2,11}, {2,3,201}, {3,3,457}, {3,2,23}, {3,2,51} };
bool bCanPlace(int8_t bx, int8_t by, byte idx) { BlockShape s=SHAPES[idx]; for(byte r=0; r<s.h; r++) for(byte c=0; c<s.w; c++) if(s.mask & (1<<(r*3+c))) { int gX=bx+c, gY=by+r; if(gX<0 || gX>=8 || gY<0 || gY>=8 || grid[gY][gX]!=0) return false; } return true; }
bool bCheckGameOver() { bool hasAny=false; for(byte i=0; i<3; i++) if(activeShapes[i]!=-1) { hasAny=true; for(int8_t y=0; y<=8-SHAPES[activeShapes[i]].h; y++) for(int8_t x=0; x<=8-SHAPES[activeShapes[i]].w; x++) if(bCanPlace(x,y,activeShapes[i])) return false; } return hasAny; }
void bGenerateShapes() { for(byte i=0; i<3; i++) activeShapes[i]=random(12); if(bCheckGameOver()) { bGameOver=true; bState=B_GAMEOVER; saveScore(8, hiBlast, bScore); } }
void bCheckLines() {
  bool clrR[8]={0}, clrC[8]={0}; byte cR=0, cC=0;
  for(byte r=0; r<8; r++) { bool f=true; for(byte c=0; c<8; c++) if(!grid[r][c]) { f=false; break; } if(f) { clrR[r]=true; cR++; } }
  for(byte c=0; c<8; c++) { bool f=true; for(byte r=0; r<8; r++) if(!grid[r][c]) { f=false; break; } if(f) { clrC[c]=true; cC++; } }
  for(byte r=0; r<8; r++) if(clrR[r]) for(byte c=0; c<8; c++) grid[r][c]=0; for(byte c=0; c<8; c++) if(clrC[c]) for(byte r=0; r<8; r++) grid[r][c]=0;
  int lines=cR+cC; if(lines>0) bScore+=(lines*100)+(lines>=2?50*(lines-1):0);
}
void blocksInit() { for(byte r=0; r<8; r++) for(byte c=0; c<8; c++) grid[r][c]=0; bScore=0; bGameOver=false; for(byte i=0; i<3; i++) activeShapes[i]=-1; bState=B_GENERATE; }

SnakePt snk[100]; int snkLen = 3; int8_t snkDX = 0, snkDY = -1; int8_t snkFX = 5, snkFY = 5; bool snkDead = false; unsigned long snkTk = 0; int snkScore = 0;
void sSpawnFood() {
  bool ok = false; 
  while(!ok) { snkFX = random(1, 14); snkFY = random(2, 25); ok = true; for(int i=0; i<snkLen; i++) if(snk[i].x == snkFX && snk[i].y == snkFY) ok = false; }
}
void snakeInit() { snkLen = 4; snkScore = 0; snkDX = 0; snkDY = -1; snkDead = false; for(int i=0; i<snkLen; i++) { snk[i].x = 7; snk[i].y = 15 + i; } sSpawnFood(); snkTk = millis(); }

void notesInit() { nSt = N_LIST; nSel = 0; nScrollY = 0; nSelAnimY = 0; }

// ─── MATH GAME ───────────────────────────────────────────────────────────────
void mathGenerate() {
  mgStartTime = millis(); mgSel = 0;
  int type = 0; 
  if (mgLevel > 3 && mgLevel <= 7) type = random(2);
  else if (mgLevel > 7) type = random(3);

  if (type == 0) {
    int maxV = 10 + mgLevel * 20; 
    int a = random(10, maxV); int b = random(10, maxV);
    if (random(2)) { mgOpStr = String(a) + "+" + String(b); mgAns = a + b; }
    else { if (a<b) {int t=a;a=b;b=t;} mgOpStr = String(a) + "-" + String(b); mgAns = a - b; }
  } else if (type == 1) {
    if (random(2)) {
      int a = random(3, 5 + mgLevel); int b = random(3, 5 + mgLevel);
      mgOpStr = String(a) + "x" + String(b); mgAns = a * b; 
    } else {
      int b = random(3, 10 + mgLevel/2); int ans = random(3, 10 + mgLevel/2);
      int a = b * ans; mgOpStr = String(a) + ":" + String(b); mgAns = ans; 
    }
  } else {
    int b = random(2, 10); int ans1 = random(2, 10); int a = b * ans1;
    int c = random(10, 50);
    if (random(2)) { mgOpStr = String(a) + ":" + String(b) + "+" + String(c); mgAns = ans1 + c; } 
    else { 
      int rem = c % ans1; if (rem == 0) rem = 1; 
      mgOpStr = String(a) + ":" + String(b) + "-" + String(rem); mgAns = ans1 - rem; 
    }
  }

  mgOpts[0] = mgAns;
  for(int i=1; i<4; i++) {
    int w; bool unique;
    do {
      unique = true;
      int r = random(3);
      if (r == 0) w = mgAns + random(1, 4) * 10; 
      else if (r == 1) w = mgAns - random(1, 4) * 10; 
      else w = mgAns + random(-4, 5); 
      
      if (w < 0) w = abs(w) + 1;
      if (w == mgAns) unique = false;
      for(int j=0; j<i; j++) if(mgOpts[j] == w) unique = false;
    } while(!unique);
    mgOpts[i] = w;
  }
  for(int i=0; i<4; i++) { int swapIdx = random(4); int temp = mgOpts[i]; mgOpts[i] = mgOpts[swapIdx]; mgOpts[swapIdx] = temp; }
}
void mathInit() { mgSt = MG_PLAY; mgLevel = 1; mathGenerate(); }

// ─── DOOM ────────────────────────────────────────────────────────────────────
#define MAP_SIZE 32
#define MAX_ENTITIES 50 
#define JOGGING_SPEED 0.005
#define GUN_TARGET_POS 18
#define GUN_SHOT_POS 22
#define E_FLOOR 0
#define E_WALL 1
#define E_ENEMY 2
#define E_MEDIKIT 3
#define E_AMMO 4
#define E_FIREBALL 5
#define S_STAND 0
#define S_ALERT 1
#define S_FIRING 2
#define S_MELEE 3
#define S_HIT 4
#define S_DEAD 5

Player player;
Entity entity[MAX_ENTITIES];
uint8_t num_entities = 0;
int current_wave = 1;
bool invert_screen = false;
int flash_screen = 0;
double doom_delta = 1.0;
uint32_t doomLastFrameTime = 0;
int gun_pos = 0;
bool gun_fired = false;
uint32_t wave_start_time = 0;
bool enemies_spawned = false;
double zbuffer[128];
bool doomRunning = false;

const uint8_t level_map[MAP_SIZE * MAP_SIZE] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,
  1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,
  1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,
  1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,
  1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

const static uint8_t bmp_gun_bits[] PROGMEM = { 0x00,0x00,0x20,0x00,0x00,0x00,0xd8,0x00,0x00,0x01,0xc4,0x00,0x00,0x02,0x04,0x00,0x00,0x02,0x02,0x00,0x00,0x02,0xea,0x00,0x00,0x04,0xd1,0x00,0x00,0x09,0x88,0x80, 0x00,0x19,0x00,0x00,0x00,0x0d,0xc2,0x80,0x00,0x29,0x81,0xc0,0x00,0x0b,0xa2,0x20,0x00,0x31,0x40,0x40,0x00,0x23,0x00,0xc0,0x00,0x13,0x00,0x40,0x00,0x72,0x02,0x00, 0x00,0x49,0x00,0x40,0x01,0xe0,0xa8,0x20,0x07,0xf1,0x00,0x30,0x0b,0xb9,0xe0,0xe8,0x07,0x5c,0x03,0xfc,0x07,0xef,0xff,0xee,0x07,0x75,0x7f,0xd2,0x1b,0xbb,0xff,0xb2, 0x11,0x57,0x7d,0x64,0x32,0xaf,0xff,0xe8,0x13,0x5f,0x75,0xd0,0x33,0xff,0xfb,0x98,0x17,0xd7,0xe5,0x00,0x1b,0x8f,0xb2,0x30,0x03,0x7d,0x58,0x10,0x6f,0xbf,0xec,0x20 };
const static uint8_t bmp_gun_mask[] PROGMEM = { 0x00,0x00,0x70,0x00,0x00,0x01,0xfc,0x00,0x00,0x03,0xfe,0x00,0x00,0x07,0xfe,0x00,0x00,0x07,0xff,0x00,0x00,0x07,0xff,0x00,0x00,0x0f,0xff,0x80,0x00,0x1f,0xff,0xc0, 0x00,0x3f,0xff,0x80,0x00,0x3f,0xff,0xc0,0x00,0x7f,0xff,0xe0,0x00,0x7f,0xff,0xf0,0x00,0x7f,0xff,0xe0,0x00,0x7f,0xff,0xe0,0x00,0x7f,0xff,0xe0,0x00,0xff,0xff,0xc0, 0x00,0xff,0xff,0xe0,0x03,0xff,0xff,0xf0,0x0f,0xff,0xff,0xf8,0x1f,0xff,0xff,0xfc,0x1f,0xff,0xff,0xfe,0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0x3f,0xff,0xff,0xff, 0x3f,0xff,0xff,0xfe,0x7f,0xff,0xff,0xfc,0x7f,0xff,0xff,0xf8,0x7f,0xff,0xff,0xfc,0x7f,0xff,0xff,0xf8,0x7f,0xff,0xff,0xf8,0x7f,0xff,0xff,0xf8,0xff,0xff,0xff,0xf0 };
const static uint8_t bmp_fire_bits[] PROGMEM = { 0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x0c,0x00,0x02,0x77,0x00,0x01,0x67,0x00,0x01,0xe5,0x80,0x01,0xe3,0xc0,0x02,0xa2,0xc0,0x07,0x82,0xe0,0x1f,0x41,0xf9, 0x9d,0x80,0x7c,0x3e,0x00,0x5e,0x76,0x00,0x6a,0x38,0x00,0x36,0x2f,0x80,0x5c,0x36,0x00,0x7e,0x3f,0x00,0x58,0x10,0x00,0x3c,0x88,0x00,0x00,0x00,0x00,0x00 };
const static uint8_t bmp_imp_bits[] PROGMEM = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x80,0x00,0x00,0x02,0x80,0x00,0x00,0x07,0x40,0x00,0x00,0x02,0x80,0x00,0x00,0x01,0x00,0x00,0x01,0x0f,0xb3,0x00, 0x00,0xd0,0x4e,0x00,0x00,0x79,0x8c,0x00,0x00,0x1c,0x19,0x00,0x01,0x8a,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x02,0x00,0x00,0x00,0x03,0x02,0x00,0x00, 0x00,0x00,0x00,0x40,0x02,0x08,0x00,0x80,0x00,0x00,0x01,0x00,0x01,0x8e,0x30,0x00,0x00,0x04,0x10,0x00,0x00,0x0c,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x10,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x20,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x01,0x80,0x00,0x00,0x02,0x40,0x00,0x00,0x03,0xe0,0x00,0x00,0x04,0x00,0x00,0x00,0x01,0xa1,0x80,0x01,0x80,0x13,0x00,0x00,0xf3,0x8a,0x00, 0x00,0x09,0x94,0x00,0x00,0x88,0x38,0x80,0x00,0x00,0x00,0x00,0x00,0x02,0x23,0x00,0x00,0x00,0x00,0x40,0x01,0x80,0x00,0x80,0x00,0x00,0x01,0x00,0x00,0xe2,0x80,0x00, 0x00,0x00,0x00,0x00,0x00,0x0c,0x20,0x00,0x00,0x04,0x30,0x00,0x00,0x02,0x20,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x20,0x00, 0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa0,0x00,0x00,0x00,0x48,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x1f,0x00,0x00,0x02,0x2a,0x80,0x00,0x01,0x05,0x00,0x00,0x01,0xae,0x20, 0x00,0x01,0x24,0x40,0x00,0x02,0xac,0x80,0x00,0x02,0x86,0x00,0x00,0x03,0x20,0x20,0x00,0x04,0x30,0x40,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x20,0x20, 0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x20,0x00,0x01,0x00,0x00,0x00,0x02,0x1a,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x38,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x98,0x00, 0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x0a,0x00, 0x00,0x00,0x08,0x40,0x00,0x00,0x00,0x80,0x00,0x01,0xd6,0x80,0x00,0x02,0xbf,0x80,0x00,0x06,0x61,0xa0,0x00,0x0c,0xe8,0x80,0x00,0x0c,0x10,0x00,0x00,0x1a,0x22,0x00, 0x00,0x12,0x40,0x00,0x00,0x06,0x0c,0x00,0x00,0x04,0x0d,0x00,0x00,0x3a,0x03,0x00,0x00,0x10,0x02,0x00,0x00,0x60,0x0a,0x00,0x00,0x50,0x04,0x00,0x00,0x20,0x03,0x00, 0x00,0x00,0x04,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
const static uint8_t bmp_imp_mask[] PROGMEM = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xc0,0x00,0x00,0x03,0xc0,0x00,0x00,0x07,0xe0,0x00,0x00,0x07,0xe0,0x00,0x00,0x03,0xe0,0x00,0x01,0x07,0xf1,0x80, 0x00,0xdf,0xfe,0x00,0x00,0x3f,0xfe,0x00,0x00,0x7f,0xff,0x00,0x01,0xff,0xff,0x80,0x00,0xff,0xff,0x80,0x01,0xff,0xff,0x80,0x03,0xcf,0xf1,0xc0,0x01,0xc7,0xf1,0xc0, 0x01,0x87,0xf1,0xc0,0x03,0x0f,0xf9,0x80,0x03,0x0f,0xfb,0x80,0x01,0x8f,0xff,0x80,0x03,0x9f,0x79,0x00,0x00,0x1f,0x7c,0x00,0x00,0x0f,0x78,0x00,0x00,0x0f,0x78,0x00, 0x00,0x07,0x30,0x00,0x00,0x07,0x38,0x00,0x00,0x07,0x30,0x00,0x00,0x07,0x30,0x00,0x00,0x03,0x78,0x00,0x00,0x07,0x30,0x00,0x00,0x0f,0x80,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x03,0xc0,0x00,0x00,0x07,0xc0,0x00,0x00,0x07,0xe0,0x00,0x00,0x07,0xc0,0x00,0x01,0x07,0xe1,0x00,0x00,0x8f,0xfa,0x00,0x00,0xff,0xfe,0x00, 0x00,0x3f,0xfe,0x00,0x01,0x7f,0xff,0x80,0x00,0xff,0xff,0x00,0x01,0xff,0xff,0x80,0x03,0xcf,0xfb,0xc0,0x03,0x87,0xf1,0xc0,0x03,0xcf,0xf3,0xc0,0x01,0xcf,0xf1,0x80, 0x00,0xcf,0xf1,0x00,0x00,0x0f,0xfb,0x80,0x00,0x1e,0x78,0x00,0x00,0x0e,0x78,0x00,0x00,0x1e,0x78,0x00,0x00,0x0f,0x70,0x00,0x00,0x0f,0x78,0x00,0x00,0x07,0x70,0x00, 0x00,0x07,0x70,0x00,0x00,0x07,0x38,0x00,0x00,0x03,0x30,0x00,0x00,0x03,0x20,0x00,0x00,0x07,0x30,0x00,0x00,0x05,0x70,0x00,0x00,0x00,0x78,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x0e,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1f,0x00,0x00,0x03,0x3f,0x80,0x00,0x01,0x3f,0x00,0x00,0x01,0xff,0x30,0x00,0x03,0xff,0xc0, 0x00,0x03,0xff,0xc0,0x00,0x03,0xff,0x80,0x00,0x07,0xff,0xe0,0x00,0x07,0xff,0xc0,0x00,0x05,0xff,0xe0,0x00,0x00,0xfc,0xe0,0x00,0x01,0xfc,0xe0,0x00,0x01,0xfc,0x70, 0x00,0x03,0xfc,0x38,0x00,0x03,0xfe,0x70,0x00,0x07,0xfc,0x00,0x00,0x07,0x9e,0x00,0x00,0x0f,0xbc,0x00,0x00,0x0f,0x3e,0x00,0x00,0x07,0x9c,0x00,0x00,0x03,0x9c,0x00, 0x00,0x03,0xb8,0x00,0x00,0x03,0x98,0x00,0x00,0x01,0x98,0x00,0x00,0x02,0x1c,0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1f,0x40,0x00,0x00,0x3e,0x80, 0x00,0x01,0xff,0x80,0x00,0x03,0xff,0x80,0x00,0x07,0xff,0xe0,0x00,0x0e,0xff,0xc0,0x00,0x0c,0xff,0x80,0x00,0x1f,0xfe,0x00,0x00,0x13,0xfc,0x00,0x00,0x07,0xfe,0x00, 0x00,0x1f,0xff,0x00,0x00,0x3f,0x9f,0x00,0x00,0x3e,0x0f,0x00,0x00,0x7c,0x0f,0x00,0x00,0x78,0x0f,0x00,0x00,0x78,0x07,0x80,0x00,0x78,0x07,0x40,0x00,0x38,0x07,0x80, 0x00,0x30,0x07,0x00,0x00,0x30,0x01,0x00,0x01,0xf0,0x00,0x00,0x01,0xb0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
const static uint8_t bmp_fireball_bits[] PROGMEM = { 0x00,0x00,0x01,0x40,0x0a,0xb0,0x0e,0xd0,0x00,0x68,0x53,0xb4,0x0f,0x48,0x27,0x78,0x17,0xa8,0x27,0xf0,0x21,0xd6,0x02,0xf8,0x20,0x48,0x06,0x20,0x01,0x00,0x00,0x00 };
const static uint8_t bmp_fireball_mask[] PROGMEM = { 0x1f,0x40,0x0f,0xf0,0x3f,0xf8,0x1f,0xfc,0x7f,0xfd,0x7f,0xfc,0x7f,0xfd,0xff,0xfe,0xff,0xff,0xff,0xff,0xff,0xfe,0xff,0xfe,0x3f,0xfe,0x17,0xf8,0x07,0xf4,0x01,0xe0 };
const static uint8_t bmp_items_bits[] PROGMEM = { 0x1f,0xf8,0x3f,0xfc,0x7f,0xfe,0x7f,0xfe,0x77,0xee,0x3f,0xfc,0x5f,0xfa,0x2f,0xf6,0x53,0xcc,0x3e,0x7e,0x5e,0x7c,0x38,0x1e,0x58,0x1c,0x3e,0x7e,0x5e,0x7e,0x2e,0xfc, 0x00,0x00,0x1f,0xf8,0x3f,0xfc,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x60,0x06,0x60,0x06,0x60,0x06,0x60,0x06,0x7f,0xfe,0x7f,0xfe,0x3f,0xfc,0x1f,0xf8 };
const static uint8_t bmp_items_mask[] PROGMEM = { 0x1f,0xf8,0x3f,0xfc,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x3f,0xfc, 0x00,0x00,0x1f,0xf8,0x3f,0xfc,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x7f,0xfe,0x3f,0xfc,0x1f,0xf8 };

uint8_t getBlockAt(int x, int y) { if (x < 0 || x >= MAP_SIZE || y < 0 || y >= MAP_SIZE) return E_WALL; return level_map[y * MAP_SIZE + x]; }
double get_dist(double x1, double y1, double x2, double y2) { return sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2)) * 20.0; }

void doomPixel(int x, int y, bool color) {
  if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
  if (color) display_buf[x + (y / 8)*128] |= (1 << (y & 7));
  else       display_buf[x + (y / 8)*128] &= ~(1 << (y & 7));
}

void doomSprite(int x, int y, const uint8_t bitmap[], const uint8_t mask[], int w, int h, int sprite, double distance) {
  int tw = (int)(w / distance); int th = (int)(h / distance);
  int byte_width = w / 8; int pixel_size = max(1, (int)(1.0 / distance));
  int sprite_offset = byte_width * h * sprite;
  for (int ty = 0; ty < th; ty += pixel_size) {
    if (y + ty < 0 || y + ty >= 56) continue;
    int sy = ty * distance;
    for (int tx = 0; tx < tw; tx += pixel_size) {
      if (x + tx < 0 || x + tx >= 128) continue;
      int sx = tx * distance;
      int byte_offset = sprite_offset + sy * byte_width + sx / 8;
      if (pgm_read_byte(mask + byte_offset) & (128 >> (sx % 8))) {
        bool pixel = pgm_read_byte(bitmap + byte_offset) & (128 >> (sx % 8));
        for (int ox_s = 0; ox_s < pixel_size; ox_s++) for (int oy = 0; oy < pixel_size; oy++) doomPixel(x + tx + ox_s, y + ty + oy, pixel);
      }
    }
  }
}

void spawnEntity(uint8_t type, double x, double y) {
  if (num_entities >= MAX_ENTITIES) return;
  entity[num_entities].uid = (((int)y << 6) | (int)x) << 4 | type;
  entity[num_entities].type = type; entity[num_entities].x = x; entity[num_entities].y = y;
  entity[num_entities].state = S_STAND; entity[num_entities].health = (type == E_ENEMY) ? 100 : 0;
  entity[num_entities].timer = 0; num_entities++;
}

void spawnFireball(double x, double y) {
  if (num_entities >= MAX_ENTITIES) return;
  entity[num_entities].uid = (((int)y << 6) | (int)x) << 4 | E_FIREBALL;
  entity[num_entities].type = E_FIREBALL; entity[num_entities].x = x; entity[num_entities].y = y;
  entity[num_entities].state = S_STAND;
  double angle = atan2(player.y - y, player.x - x) * 180.0 / PI;
  if (angle < 0) angle += 360.0;
  entity[num_entities].health = (int)angle; num_entities++;
}

void removeEntity(int index) { for (int i = index; i < num_entities - 1; i++) entity[i] = entity[i + 1]; num_entities--; }

void spawnWave(int wave) {
  num_entities = 0; player.x = MAP_SIZE / 2.0; player.y = MAP_SIZE / 2.0; 
  wave_start_time = millis(); enemies_spawned = false;
  int items_to_spawn = 3 + wave / 2;
  for(int i = 0; i < items_to_spawn; i++) {
    int rx, ry;
    while(true) { rx = random(1, MAP_SIZE - 1); ry = random(1, MAP_SIZE - 1); if (getBlockAt(rx, ry) != E_WALL) break; }
    spawnEntity(E_AMMO, rx + 0.5, ry + 0.5);
    while(true) { rx = random(1, MAP_SIZE - 1); ry = random(1, MAP_SIZE - 1); if (getBlockAt(rx, ry) != E_WALL) break; }
    spawnEntity(E_MEDIKIT, rx + 0.5, ry + 0.5);
  }
}

uint16_t detectCollision(double px, double py, double rel_x, double rel_y, bool walls_only) {
  int round_x = (int)(px + rel_x); int round_y = (int)(py + rel_y);
  if (getBlockAt(round_x, round_y) == E_WALL) return 1;
  if (walls_only) return 0;
  for (int i = 0; i < num_entities; i++) {
    if (entity[i].x == px && entity[i].y == py) continue; 
    if (entity[i].type != E_ENEMY || entity[i].state == S_DEAD) continue;
    double dist = get_dist(px, py, entity[i].x - rel_x, entity[i].y - rel_y);
    if (dist < 4.0 && dist < entity[i].distance) return entity[i].uid;
  }
  return 0;
}

uint16_t updatePosition(double &px, double &py, double rel_x, double rel_y, bool walls_only) {
  uint16_t collide_x = detectCollision(px, py, rel_x, 0, walls_only);
  uint16_t collide_y = detectCollision(px, py, 0, rel_y, walls_only);
  if (!collide_x) px += rel_x; if (!collide_y) py += rel_y;
  return collide_x || collide_y;
}

void translateIntoView(double px, double py, double &tx, double &ty) {
  double sprite_x = px - player.x; double sprite_y = py - player.y;
  double inv_det = 1.0 / (player.plane_x * player.dir_y - player.dir_x * player.plane_y);
  tx = inv_det * (player.dir_y * sprite_x - player.dir_x * sprite_y);
  ty = inv_det * (-player.plane_y * sprite_x + player.plane_x * sprite_y);
}

void fire() {
  for (int i = 0; i < num_entities; i++) {
    if (entity[i].type != E_ENEMY || entity[i].state == S_DEAD) continue;
    double tx, ty; translateIntoView(entity[i].x, entity[i].y, tx, ty);
    if (abs(tx) < 20.0 && ty > 0) {
      int damage = max(0, (int)(15.0 - ty));
      if (damage > 0) { entity[i].health = max(0, entity[i].health - damage); entity[i].state = S_HIT; entity[i].timer = 4; }
    }
  }
}

void updateEntities() {
  if (!enemies_spawned && (millis() - wave_start_time > 3000)) {
    int enemies_to_spawn = 2 + pow(1.5, current_wave); if (enemies_to_spawn > 30) enemies_to_spawn = 30;
    for(int i = 0; i < enemies_to_spawn; i++) {
      int rx, ry;
      while(true) { rx = random(1, MAP_SIZE - 1); ry = random(1, MAP_SIZE - 1); if (getBlockAt(rx, ry) != E_WALL && get_dist(player.x, player.y, rx + 0.5, ry + 0.5) >= 100.0) break; }
      spawnEntity(E_ENEMY, rx + 0.5, ry + 0.5);
    }
    enemies_spawned = true;
  }
  int enemies_alive = 0;
  for (int i = 0; i < num_entities; i++) {
    entity[i].distance = get_dist(player.x, player.y, entity[i].x, entity[i].y);
    if (entity[i].timer > 0) entity[i].timer--;
    if (entity[i].type == E_ENEMY && entity[i].state != S_DEAD) enemies_alive++;
    switch (entity[i].type) {
      case E_ENEMY:
        if (entity[i].health <= 0) { if (entity[i].state != S_DEAD) { entity[i].state = S_DEAD; entity[i].timer = 6; } } 
        else if (entity[i].state == S_HIT || entity[i].state == S_FIRING) { if (entity[i].timer == 0) { entity[i].state = S_ALERT; entity[i].timer = 40; } } 
        else {
          if (entity[i].distance > 120.0 && entity[i].distance < 400.0) {
            if (entity[i].state != S_ALERT) { entity[i].state = S_ALERT; entity[i].timer = 20; }
            else if (entity[i].timer == 0) { spawnFireball(entity[i].x, entity[i].y); entity[i].state = S_FIRING; entity[i].timer = 6; } 
            else { double dx = (player.x > entity[i].x ? 1 : -1) * 0.03 * doom_delta; double dy = (player.y > entity[i].y ? 1 : -1) * 0.03 * doom_delta; updatePosition(entity[i].x, entity[i].y, dx, dy, true); }
          } else if (entity[i].distance <= 120.0) {
            if (entity[i].state != S_MELEE) { entity[i].state = S_MELEE; entity[i].timer = 10; }
            else if (entity[i].timer == 0) { player.health = max(0, player.health - min(15, 2 + current_wave * 2)); entity[i].timer = 14; flash_screen = 1; }
          } else entity[i].state = S_STAND;
        } break;
      case E_FIREBALL:
        if (entity[i].distance < 15.0) { player.health = max(0, player.health - min(25, 5 + current_wave * 3)); flash_screen = 1; removeEntity(i); i--; } 
        else { double rad = entity[i].health * PI / 180.0; if (updatePosition(entity[i].x, entity[i].y, cos(rad) * 0.2, sin(rad) * 0.2, true)) { removeEntity(i); i--; } } break;
      case E_MEDIKIT:
        if (entity[i].distance < 15.0) { player.health = min(100, player.health + 50); flash_screen = 1; removeEntity(i); i--; } break;
      case E_AMMO:
        if (entity[i].distance < 15.0) { player.ammo += 15; flash_screen = 1; removeEntity(i); i--; } break;
    }
  }
  if (enemies_spawned && enemies_alive == 0 && player.health > 0) { current_wave++; player.health = min(100, player.health + 20); spawnWave(current_wave); }
}

void doomInit() {
  player.dir_x = 1.0; player.dir_y = 0.0; player.plane_x = 0.0; player.plane_y = -0.66;
  player.health = 100; player.ammo = 20; current_wave = 1; invert_screen = false; flash_screen = 0;
  display.invertDisplay(false); spawnWave(current_wave); doomLastFrameTime = millis();
}

void handleRoot() { server.send(200, "text/html", F("<!DOCTYPE html><html><body style='background:#121212;color:#fff;text-align:center;font-family:sans-serif;'><br><h2>ESP32 TIME SETUP</h2><form action='/set'><input type='number' name='h' required>:<input type='number' name='m' required>:<input type='number' name='s' required><br><br><button type='submit' style='padding:10px 20px;'>SET TIME</button></form></body></html>")); }
void handleSetTime() { 
  if(server.hasArg("h")){ 
    struct tm t; getLocalTime(&t, 0); 
    t.tm_hour = server.arg("h").toInt(); t.tm_min = server.arg("m").toInt(); t.tm_sec = server.arg("s").toInt();
    time_t ts = mktime(&t); struct timeval now = { .tv_sec = ts }; settimeofday(&now, NULL);
  } 
  server.send(200, "text/html", F("<html><head><meta http-equiv='refresh' content='1;url=/'></head><body style='background:#121212;color:#0f0;text-align:center;'><br><h2>SAVED!</h2></body></html>")); 
}

void drawMenuIcon(int x, int y, byte i, bool inv) {
  int c = inv ? BLACK : WHITE;
  if(i==0){ display.drawCircle(x+4,y+5,4,c); display.drawLine(x+4,y+5,x+4,y+2,c); display.drawLine(x+4,y+5,x+6,y+5,c); } 
  else if(i==1){ display.drawRect(x,y,10,12,c); display.drawLine(x+2,y+5,x+4,y+5,c); display.drawLine(x+6,y+5,x+8,y+5,c); display.drawLine(x+3,y+4,x+3,y+6,c); } 
  else if(i==2){ display.drawLine(x+1,y+6,x+5,y+2,c); display.drawLine(x+5,y+2,x+9,y+6,c); display.drawRect(x+1,y+7,8,4,c); } 
  else if(i==3){ display.fillCircle(x+5,y+10,1,c); display.drawCircleHelper(x+5,y+10,4,3,c); display.drawCircleHelper(x+5,y+10,7,3,c); } 
  else if(i==4){ display.drawRect(x+2,y+8,4,4,c); display.drawRect(x+6,y+8,4,4,c); display.drawRect(x+2,y+4,4,4,c); display.drawRect(x+2,y,4,4,c); } 
  else if(i==5){ display.drawRect(x,y,4,4,c); display.drawRect(x+5,y,4,4,c); display.drawRect(x,y+5,4,4,c); display.drawRect(x+5,y+5,4,4,c); } 
  else if(i==6){ display.drawRoundRect(x,y,10,6,2,c); display.drawPixel(x+2,y+2,c); display.drawPixel(x+8,y+2,c); } 
  else if(i==7){ display.drawRoundRect(x+2,y,6,10,3,c); display.drawLine(x+5,y,x+5,y+3,c); } 
  else if(i==8){ display.drawRect(x+2,y+2,6,6,c); display.drawLine(x,y,x+4,y+4,c); } 
  else if(i==9){ display.drawCircle(x+4,y+4,3,c); display.drawLine(x+4,y,x+4,y+8,c); display.drawLine(x,y+4,x+8,y+4,c); } 
  else if(i==10){ display.drawRect(x+1,y+1,8,10,c); display.drawLine(x+3,y+3,x+7,y+3,c); display.drawLine(x+3,y+5,x+7,y+5,c); display.drawLine(x+3,y+7,x+5,y+7,c); } 
  else if(i==11){ display.drawRect(x,y,10,10,c); display.drawLine(x+5,y+2,x+5,y+8,c); display.drawLine(x+2,y+5,x+8,y+5,c); } 
}

void drawMenu(int ox) {
  int drawBoxY = (int)menuBoxY - (int)menuScrollY;
  if(drawBoxY > -20 && drawBoxY < 128) { display.fillRoundRect(ox + 2, drawBoxY, 60, 18, 3, WHITE); }
  for (byte i = 0; i < NUM_APPS; i++) { 
    int y = 4 + i * 20 - (int)menuScrollY; 
    if (y > -20 && y < 128) {
      bool isS = (mSel == i);
      display.setTextColor(isS ? BLACK : WHITE);
      drawMenuIcon(ox + 6, y + 2, i, isS);
      display.setCursor(ox + 20, y + 5); 
      display.print(APP_NAMES[i]);
    }
  }
}

void drawTime(int ox) {
  int cx = ox + 32, cy = 36, r = 26;
  display.drawCircle(cx, cy, r, WHITE);
  for(int i=0; i<12; i++) { float a = i * PI / 6.0; display.drawPixel(cx + r*0.8*cos(a), cy + r*0.8*sin(a), WHITE); }
  float aSec = sc * PI / 30.0 - PI / 2.0; display.drawLine(cx, cy, cx + r*0.85*cos(aSec), cy + r*0.85*sin(aSec), WHITE);
  float aMin = mn * PI / 30.0 - PI / 2.0; display.drawLine(cx, cy, cx + r*0.7*cos(aMin), cy + r*0.7*sin(aMin), WHITE); display.drawLine(cx+1, cy, cx+1 + r*0.7*cos(aMin), cy + r*0.7*sin(aMin), WHITE);
  float aHr = (hr%12 + mn/60.0) * PI / 6.0 - PI / 2.0; display.drawLine(cx, cy, cx + r*0.5*cos(aHr), cy + r*0.5*sin(aHr), WHITE); display.drawLine(cx+1, cy, cx+1 + r*0.5*cos(aHr), cy + r*0.5*sin(aHr), WHITE);
  
  display.setTextColor(WHITE); display.setTextSize(2);
  String hm = (hr<10?"0":"")+String(hr)+":"+(mn<10?"0":"")+String(mn);
  printCenter(hm, ox, 72, 64); display.setTextSize(1);
  printCenter(wifiTimeSynced ? F("WiFi Sync") : F("No Sync"), ox, 96, 64);
}

void drawCalc(int ox) {
  display.drawRoundRect(ox+2, 2, 60, 22, 3, WHITE); display.setTextColor(WHITE); display.setCursor(ox+6, 5); 
  if (oper) { 
    display.print(op1); display.print(F(" ")); 
    if (oper == '*') display.print('x');
    else if (oper == '/') display.print(':');
    else display.print(oper); 
  } 
  printRight(String(dv), ox+58, 14); 
  display.fillRoundRect(ox+(int)calcX, (int)calcY, 14, cEng?17:20, 3, WHITE);
  for(byte r=0; r<cMaxRows; r++) for(byte c=0; c<4; c++) {
      int x = ox+3+c*15, y = (cEng?26:36)+r*(cEng?19:22); display.drawRoundRect(x, y, 14, cEng?17:20, 3, WHITE);
      bool isS = (r==cRow && c==cCol); int col = isS ? BLACK : WHITE; char k = cEng ? KEYS_ENG[r][c] : KEYS_NORM[r][c]; 
      
      if (k == '*') {
        int cx = x + 7, cy = y + (cEng?8:10);
        display.drawLine(cx-2, cy-2, cx+2, cy+2, col);
        display.drawLine(cx-2, cy+2, cx+2, cy-2, col);
      } else if (k == '/') {
        int cx = x + 7, cy = y + (cEng?8:10);
        display.drawLine(cx-2, cy, cx+2, cy, col);
        display.drawPixel(cx, cy-3, col);
        display.drawPixel(cx, cy+3, col);
      } else {
        display.setTextColor(col); String tmp = String(k); printCenter(tmp, x, y+(cEng?4:6), 14);
      }
  }
}

void drawCarSprite(int x, int y, bool isEnemy) {
  display.drawRoundRect(x, y, 10, 16, 2, WHITE); 
  if (!isEnemy) display.fillRect(x+2, y+10, 6, 4, WHITE); else display.fillRect(x+2, y+2, 6, 4, WHITE); 
  display.fillRect(x-1, y+2, 2, 4, WHITE); display.fillRect(x+9, y+2, 2, 4, WHITE);
  display.fillRect(x-1, y+10, 2, 4, WHITE); display.fillRect(x+9, y+10, 2, 4, WHITE);
}
void drawRacer(int ox) {
  display.setTextColor(WHITE); display.setCursor(ox+2, 2); display.print(F("S:")); display.print(rScore);
  display.setCursor(ox+32, 2); display.print(F("H:")); display.print(hiRacer);
  display.drawLine(ox+4, 12, ox+4, 128, WHITE); display.drawLine(ox+59, 12, ox+59, 128, WHITE);
  
  int roadSpeed = max(5, 20 - (rScore / 2));
  int roadY = (millis() / roadSpeed) % 20; 
  for(int i=0; i<128; i+=20) { display.drawLine(ox+32, i+roadY, ox+32, i+roadY+10, WHITE); }
  
  if (rGameOver) { display.fillRoundRect(ox+4, 40, 56, 42, 4, BLACK); display.drawRoundRect(ox+4, 40, 56, 42, 4, WHITE); printCenter(F("CRASHED"), ox, 48, 64); printCenter(F(">> SEL"), ox, 66, 64); } 
  else { drawCarSprite(ox + (int)rX, 108, false); for(byte i=0; i<3; i++) if (rObs[i].y > -20 && rObs[i].y < 130) drawCarSprite(ox + rObs[i].x, (int)rObs[i].y, true); }
}

void drawWifi(int ox) {
  display.setTextColor(WHITE);
  if(wSt==W_SCANNING) { printCenter(F("SCAN"), ox, 4, 64); int cx=ox+32, cy=64, r=24; display.drawCircle(cx,cy,r,WHITE); display.drawLine(cx,cy,cx+r*cos(millis()*0.003),cy+r*sin(millis()*0.003),WHITE); }
  else if(wSt==W_LIST) {
    printCenter(F("NETS"), ox, 4, 64); display.drawLine(ox,14,ox+64,14,WHITE);
    if(wCt==0) printCenter(F("No signal"), ox, 40, 64);
    else { for(int i=0; i<wCt; i++) { int y = 18+i*20+(int)wifiSY; if(y>10 && y<120) { if(wSel==i){ display.fillRoundRect(ox+2,y,60,18,3,WHITE); display.setTextColor(BLACK); display.setCursor(ox+5,y+5); String ssid = wNet[i].ssid; if (ssid.length() > 6) { String pad = ssid + F("   "); int l = pad.length(); int s = (millis() / 400) % l; display.print((pad.substring(s) + pad.substring(0, s)).substring(0, 6)); } else display.print(ssid); } else { display.setTextColor(WHITE); display.setCursor(ox+5,y+5); display.print(wNet[i].ssid.substring(0,6)); } } } }
  } else if(wSt==W_KEYBOARD) {
    display.drawRoundRect(ox+2,10,60,22,4,WHITE); display.setTextColor(WHITE); String s = String(wPwd) + (((millis()/350)%2==0)?"_":" "); printRight(s, ox+56, 17);
    display.fillRoundRect(ox+(int)vkX, (int)vkY, 16, 16, 3, WHITE); for(byte r=0; r<4; r++) for(byte c=0; c<3; c++) { int x=ox+5+c*18, y=40+r*20; display.drawRoundRect(x,y,16,16,3,WHITE); bool isS = (r==vkR && c==vkC); int col = isS ? BLACK : WHITE; char k = VK[r][c]; display.setTextColor(col); String t = String(k); printCenter(t,x,y+4,16); }
  } else if(wSt==W_CONNECTING) { display.setTextWrap(true); display.setCursor(ox+2, 2); if (connStep == 1) display.print(F("Connecting...")); else if (connStep == 2) { display.print(F("Host:")); display.setCursor(ox+2, 24); display.print(wNet[wSel].ssid); } display.setTextWrap(false); display.drawRoundRect(ox+7, 56, 50, 6, 3, WHITE); display.fillRoundRect(ox+7, 56, ((millis()/20)%50), 6, 3, WHITE);
  } else if(wSt==W_CONNECTED) { printCenter(F("Success!"), ox, 20, 64); printCenter(WiFi.localIP().toString(), ox, 50, 64); } else if(wSt==W_FAILED) { printCenter(F("Error/TimeOut"), ox, 36, 64); }
}

void drawTBlock(int x, int y) { display.fillRect(x, y, 5, 4, WHITE); }

void drawTetris(int ox) {
  display.drawRect(ox+1, 24, 62, 102, WHITE); display.setTextColor(WHITE); 
  display.setCursor(ox+2, 2); display.print(F("S:")); display.print(tSc);
  display.setCursor(ox+32, 2); display.print(F("H:")); display.print(hiTetris);
  
  if(tDead) { display.fillRoundRect(ox+4,40,56,40,4,BLACK); display.drawRoundRect(ox+4,40,56,40,4,WHITE); printCenter(F("OVER"), ox, 56, 64); return; }
  for(int r=0; r<20; r++) for(int c=0; c<10; c++) if((tBrd[r] & (1<<c)) != 0) drawTBlock(ox+2+c*6, 25+r*5);
  uint16_t p = TET[tTy][tRot]; for(int i=0; i<4; i++) for(int j=0; j<4; j++) if((p & (1 << (15 - (i*4+j)))) != 0 && tY+i >= 0) drawTBlock(ox+2+(tX+j)*6, 25+(tY+i)*5);
}

void drawBlocks(int ox) {
  display.setTextColor(WHITE); 
  display.setCursor(ox+2, 2); display.print(F("S:")); display.print(bScore); 
  display.setCursor(ox+32, 2); display.print(F("H:")); display.print(hiBlast);
  display.drawRect(ox + 11, 19, 42, 42, WHITE);
  for (byte r=0; r<8; r++) for (byte c=0; c<8; c++) { int cx = ox + 12 + c*5, cy = 20 + r*5; if (grid[r][c] != 0) display.fillRect(cx, cy, 4, 4, WHITE); }
  if (bState == B_PLACE) { BlockShape s = SHAPES[activeShapes[selectedShapeIdx]]; for (byte r = 0; r < s.h; r++) for (byte c = 0; c < s.w; c++) if (s.mask & (1 << (r * 3 + c))) display.fillRect(ox + 12 + (blockX + c) * 5, 20 + (blockY + r) * 5, 4, 4, WHITE); }
  for (byte i=0; i<3; i++) {
    int sx = ox + 4 + i * 20, sy = 75;
    if (activeShapes[i] != -1) { BlockShape s = SHAPES[activeShapes[i]]; for (byte r = 0; r < s.h; r++) for (byte c = 0; c < s.w; c++) if (s.mask & (1 << (r * 3 + c))) { if (bState == B_SELECT && selectedShapeIdx == i) display.fillRect(sx + c*4, sy + r*4, 3, 3, WHITE); else display.drawRect(sx + c*4, sy + r*4, 3, 3, WHITE); } if (bState == B_SELECT && selectedShapeIdx == i) display.drawRoundRect(sx - 2, sy - 4, 16, 18, 2, WHITE); }
  }
  if (bState == B_GAMEOVER) { display.fillRoundRect(ox + 4, 40, 56, 40, 4, BLACK); display.drawRoundRect(ox + 4, 40, 56, 40, 4, WHITE); printCenter(F("OVER"), ox, 48, 64); printCenter(F(">> SEL"), ox, 66, 64); }
}

void drawSnake(int ox) {
  display.drawRect(ox+1, 19, 62, 109, WHITE); 
  display.setTextColor(WHITE); display.setCursor(ox+2, 2); display.print(F("S:")); display.print(snkScore);
  display.setCursor(ox+32, 2); display.print(F("H:")); display.print(hiSnake);
  
  if(snkDead) { display.fillRoundRect(ox+4, 40, 56, 40, 4, BLACK); display.drawRoundRect(ox+4, 40, 56, 40, 4, WHITE); printCenter(F("OVER"), ox, 48, 64); printCenter(F(">> SEL"), ox, 66, 64); return; }
  
  int fx = ox + 2 + snkFX*4, fy = 20 + snkFY*4;
  display.fillCircle(fx+2, fy+2, 2, WHITE); display.drawPixel(fx+2, fy-1, WHITE);
  
  for(int i=1; i<snkLen; i++) display.fillRect(ox + 2 + snk[i].x*4, 20 + snk[i].y*4, 3, 3, WHITE);
  
  int hx = ox + 2 + snk[0].x*4, hy = 20 + snk[0].y*4;
  display.fillRect(hx, hy, 4, 4, WHITE);
  if(snkDX == 1) { display.drawPixel(hx+2, hy+1, BLACK); display.drawPixel(hx+2, hy+2, BLACK); } 
  else if(snkDX == -1) { display.drawPixel(hx+1, hy+1, BLACK); display.drawPixel(hx+1, hy+2, BLACK); } 
  else if(snkDY == -1) { display.drawPixel(hx+1, hy+1, BLACK); display.drawPixel(hx+2, hy+1, BLACK); } 
  else { display.drawPixel(hx+1, hy+2, BLACK); display.drawPixel(hx+2, hy+2, BLACK); } 
}

void drawBLE(int ox) {
  display.setTextColor(WHITE); printCenter(F("MOUSE"), ox, 4, 64); display.drawLine(ox+4, 14, ox+60, 14, WHITE);
  if (isBleConnected) {
    printCenter(F("Connected!"), ox, 20, 64);
    display.drawRoundRect(ox + 24, 32, 16, 26, 6, WHITE); display.drawLine(ox + 24, 42, ox + 40, 42, WHITE); display.drawLine(ox + 32, 32, ox + 32, 42, WHITE);
    if(btnState[4]) display.fillRoundRect(ox + 24, 32, 8, 10, 3, WHITE); if(btnState[3]) display.fillRoundRect(ox + 32, 32, 8, 10, 3, WHITE); 
  } else { printCenter(F("Pairing..."), ox, 24, 64); display.drawRoundRect(ox+7, 40, 50, 6, 3, WHITE); display.fillRoundRect(ox+7, 40, ((millis()/20)%50), 6, 3, WHITE); }
}

void drawADXL(int ox) {
  display.setTextColor(WHITE);
  if (adxlSt == ADXL_MENU) {
    printCenter(F("ADXL MODE"), ox, 10, 64);
    display.fillRoundRect(ox+4, 30 + (int)adxlSelAnimY, 56, 14, 2, WHITE);
    display.setTextColor(adxlSel == 0 ? BLACK : WHITE);
    if (adxlSel != 0) display.drawRoundRect(ox+4, 30, 56, 14, 2, WHITE);
    printCenter(F("3D CUBE"), ox, 33, 64);
    display.setTextColor(adxlSel == 1 ? BLACK : WHITE);
    if (adxlSel != 1) display.drawRoundRect(ox+4, 50, 56, 14, 2, WHITE);
    printCenter(F("BALLS"), ox, 53, 64);
  } 
  else if (adxlSt == ADXL_CUBE) {
    display.setCursor(ox+2, 2); display.print(F("X:")); display.print(adxlX);
    display.setCursor(ox+2, 12); display.print(F("Y:")); display.print(adxlY);
    display.setCursor(ox+2, 22); display.print(F("Z:")); display.print(adxlZ);
    int cx = ox + 32, cy = 74; 
    static float smoothPitch = 0, smoothRoll = 0;
    float targetPitch = atan2(adxlY, sqrt((float)adxlX*adxlX + (float)adxlZ*adxlZ)) * 1.8;
    float targetRoll = atan2(-adxlX, sqrt((float)adxlY*adxlY + (float)adxlZ*adxlZ)) * 1.8;
    smoothPitch += (targetPitch - smoothPitch) * 0.3; smoothRoll += (targetRoll - smoothRoll) * 0.3;
    const float SIZE = 12.0;
    float verts[8][3] = { {-SIZE, -SIZE, -SIZE}, { SIZE, -SIZE, -SIZE}, { SIZE,  SIZE, -SIZE}, {-SIZE,  SIZE, -SIZE}, {-SIZE, -SIZE,  SIZE}, { SIZE, -SIZE,  SIZE}, { SIZE,  SIZE,  SIZE}, {-SIZE,  SIZE,  SIZE} };
    int proj[8][2];
    for(int i=0; i<8; i++) {
      float x = verts[i][0], y = verts[i][1], z = verts[i][2];
      float xy = y * cos(smoothPitch) - z * sin(smoothPitch); float xz = y * sin(smoothPitch) + z * cos(smoothPitch); y = xy; z = xz;
      float yx = x * cos(smoothRoll) + z * sin(smoothRoll); float yz = -x * sin(smoothRoll) + z * cos(smoothRoll); x = yx; z = yz;
      float dist = 40.0; float w = dist / (dist + z);
      proj[i][0] = cx + (int)(x * w * 1.5); proj[i][1] = cy + (int)(y * w * 1.5);
    }
    const int edges[12][2] = { {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4}, {0,4}, {1,5}, {2,6}, {3,7} };
    for(int i=0; i<12; i++) display.drawLine(proj[edges[i][0]][0], proj[edges[i][0]][1], proj[edges[i][1]][0], proj[edges[i][1]][1], WHITE);
    printCenter(F("3D CUBE"), ox, 116, 64);
  }
  else if (adxlSt == ADXL_BALLS) {
    for (int i = 0; i < NUM_BALLS; i++) display.fillCircle(ox + (int)balls[i].x, (int)balls[i].y, (int)balls[i].radius, WHITE);
  }
}

void drawNotes(int ox) {
  display.setTextColor(WHITE);
  if (nSt == N_LIST) {
    int activeCount = 0; for (int i = 0; i < MAX_NOTES; i++) if (notes[i].inUse) activeCount++;
    int selY = 16 + (int)nSelAnimY - (int)nScrollY;
    if (selY > 12 && selY < 128) display.fillRoundRect(ox+2, selY-2, 60, 15, 2, WHITE);

    for (int i = 0; i <= activeCount; i++) {
      if (i == MAX_NOTES) break;
      int y = 16 + i * 16 - (int)nScrollY;
      if (y > 4 && y < 128) { 
        bool isSel = (nSel == i); display.setTextColor(isSel ? BLACK : WHITE);
        if (i < activeCount) {
          display.drawCircle(ox+7, y+4, 4, isSel ? BLACK : WHITE);
          if (notes[i].isTicked) {
            display.drawLine(ox+5, y+4, ox+7, y+6, isSel ? BLACK : WHITE);
            display.drawLine(ox+7, y+6, ox+10, y+2, isSel ? BLACK : WHITE);
          }
          display.setCursor(ox+14, y); display.print(notes[i].title);
        } else { display.setCursor(ox+4, y); display.print(F("+ add note")); }
      }
    }
    display.fillRect(ox, 0, 64, 14, BLACK); display.setTextColor(WHITE);
    printCenter(F("NOTES"), ox, 2, 64); display.drawLine(ox+4, 12, ox+60, 12, WHITE);
  } 
  else if (nSt == N_ACTION) {
    printCenter(notes[nSel].title, ox, 2, 64); display.drawRoundRect(ox+2, 14, 60, 70, 3, WHITE);
    const char* acts[] = {"text", "title", "look", "delete", "tick"};
    display.fillRoundRect(ox+4, 18 + (int)nActAnimY, 56, 13, 2, WHITE);
    for (int i = 0; i < 5; i++) {
      display.setTextColor(nActSel == i ? BLACK : WHITE);
      display.setCursor(ox+6, 20 + i * 13); display.print(acts[i]);
    }
  }
  else if (nSt == N_LOOK) {
    display.drawRoundRect(ox+2, 2, 60, 124, 2, WHITE); printCenter(notes[nSel].title, ox, 6, 64); display.drawLine(ox+4, 16, ox+60, 16, WHITE);
    String t = String(notes[nSel].text); int cx = ox+6, cy = 20 - (int)nLookScrollY;
    for(int i=0; i<t.length(); i++) { if (cy > 16 && cy < 120) { display.setCursor(cx, cy); display.print(t[i]); } cx += 6; if(cx > ox+52) { cx = ox+6; cy += 10; } }
  }
  else if (nSt == N_DEL) {
    display.drawRoundRect(ox+2, 30, 60, 40, 3, WHITE); printCenter(F("Delete?"), ox, 36, 64);
    if(nActSel==0) display.fillRoundRect(ox+6, 54, 22, 12, 2, WHITE); else display.drawRoundRect(ox+6, 54, 22, 12, 2, WHITE);
    if(nActSel==1) display.fillRoundRect(ox+36, 54, 22, 12, 2, WHITE); else display.drawRoundRect(ox+36, 54, 22, 12, 2, WHITE);
    display.setTextColor(nActSel==0 ? BLACK : WHITE); display.setCursor(ox+10, 56); display.print(F("no"));
    display.setTextColor(nActSel==1 ? BLACK : WHITE); display.setCursor(ox+38, 56); display.print(F("yes"));
  }
  else if (nSt == N_EDIT_TITLE || nSt == N_EDIT_TEXT) {
    display.drawRoundRect(ox+2, 2, 60, 32, 2, WHITE);
    String txt = String(nKbdBuf); if ((millis()/300)%2==0) txt += "_";
    int cursorY = 4; int cursorX = ox+4;
    for(int i=0; i<txt.length(); i++) { display.setCursor(cursorX, cursorY); display.print(txt[i]); cursorX += 6; if(cursorX > ox + 54) { cursorX = ox+4; cursorY += 9; } }
    
    for(int r=0; r<=8; r++) display.drawLine(ox+2, 36+r*11, ox+62, 36+r*11, WHITE);
    for(int c=0; c<=5; c++) display.drawLine(ox+2+c*12, 36, ox+2+c*12, 36+8*11, WHITE);

    kbdAnimX = ease(kbdAnimX, nKbdX * 12, 0.4); kbdAnimY = ease(kbdAnimY, nKbdY * 11, 0.4);
    display.fillRoundRect(ox + 3 + (int)kbdAnimX, 37 + (int)kbdAnimY, 11, 10, 1, WHITE);

    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 5; c++) {
        int px = ox + 2 + c * 12; int py = 36 + r * 11;
        bool isSel = (r == nKbdY && c == nKbdX);
        int iconColor = isSel ? BLACK : WHITE;
        display.setTextColor(iconColor);
        
        char key = N_KBD_LAYOUT[r][c];
        if(key == 'V') { display.drawLine(px+3, py+5, px+5, py+7, iconColor); display.drawLine(px+5, py+7, px+9, py+3, iconColor); } 
        else if(key == 'X') { display.drawLine(px+4, py+3, px+8, py+7, iconColor); display.drawLine(px+8, py+3, px+4, py+7, iconColor); } 
        else if(key == '<') { display.drawLine(px+3, py+5, px+6, py+2, iconColor); display.drawLine(px+3, py+5, px+6, py+8, iconColor); display.drawLine(px+6, py+2, px+10, py+2, iconColor); display.drawLine(px+6, py+8, px+10, py+8, iconColor); display.drawLine(px+10, py+2, px+10, py+8, iconColor); } 
        else if(key == '_') { display.setCursor(px + 4, py + 2); display.print(F("-")); }
        else { display.setCursor(px + 4, py + 2); display.print(key); }
      }
    }
  }
}

void drawMath(int ox) {
  display.setTextColor(WHITE);
  if (mgSt == MG_PLAY) {
    int timeLeft = 15000 - (millis() - mgStartTime); if (timeLeft < 0) timeLeft = 0;
    int barW = map(timeLeft, 0, 15000, 60, 0);
    
    display.drawRoundRect(ox+2, 2, 60, 6, 2, WHITE); display.fillRoundRect(ox+2, 2, barW, 6, 2, WHITE);
    
    display.setCursor(ox+2, 12); display.print(F("L:")); display.print(mgLevel);
    display.setCursor(ox+32, 12); display.print(F("H:")); display.print(hiMath);
    
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(mgOpStr, 0, 0, &x1, &y1, &w, &h);
    int eqX = ox + (64 - w) / 2;
    display.setCursor(eqX, 24); display.print(mgOpStr);
    display.setCursor(eqX + 1, 24); display.print(mgOpStr); 
    
    display.fillRoundRect(ox + 4, 40 + (int)mgSelAnimY, 56, 14, 3, WHITE);
    for(int i=0; i<4; i++) {
      int px = ox + 4, py = 40 + i * 18; 
      display.setTextColor(mgSel == i ? BLACK : WHITE); 
      if (mgSel != i) display.drawRoundRect(px, py, 56, 14, 3, WHITE); 
      display.setCursor(px+4, py+3); display.print((char)('A'+i)); display.print(F(" ")); display.print(mgOpts[i]);
    }
  } else if (mgSt == MG_OVER) {
    display.fillRoundRect(ox+4, 30, 56, 40, 3, BLACK); display.drawRoundRect(ox+4, 30, 56, 40, 3, WHITE);
    printCenter(F("GAME OVER"), ox, 42, 64); 
    printCenter(String(F("LVL ")) + String(mgLevel), ox, 56, 64); 
  }
}

void runDoomFrame() {
  if (millis() - doomLastFrameTime < 33) return; 
  doom_delta = (double)(millis() - doomLastFrameTime) / 33.3;
  doomLastFrameTime = millis();

  memset(display_buf, 0, 1024); 

  double jogging = 0;
  double view_height = 0;

  if (player.health > 0) {
    if (btnState[3]) { player.velocity += (0.2 - player.velocity) * 0.4; jogging = abs(player.velocity) * 5.0; } 
    else if (btnState[2]) { player.velocity += (-0.2 - player.velocity) * 0.4; jogging = abs(player.velocity) * 5.0; } 
    else { player.velocity *= 0.5; jogging = abs(player.velocity) * 5.0; }

    if (btnState[1]) { 
      double rot = 0.12 * doom_delta;
      double old_dir_x = player.dir_x;
      player.dir_x = player.dir_x * cos(-rot) - player.dir_y * sin(-rot);
      player.dir_y = old_dir_x * sin(-rot) + player.dir_y * cos(-rot);
      double old_plane_x = player.plane_x;
      player.plane_x = player.plane_x * cos(-rot) - player.plane_y * sin(-rot);
      player.plane_y = old_plane_x * sin(-rot) + player.plane_y * cos(-rot);
    } else if (btnState[0]) { 
      double rot = 0.12 * doom_delta;
      double old_dir_x = player.dir_x;
      player.dir_x = player.dir_x * cos(rot) - player.dir_y * sin(rot);
      player.dir_y = old_dir_x * sin(rot) + player.dir_y * cos(rot);
      double old_plane_x = player.plane_x;
      player.plane_x = player.plane_x * cos(rot) - player.plane_y * sin(rot);
      player.plane_y = old_plane_x * sin(rot) + player.plane_y * cos(rot);
    }

    view_height = abs(sin(millis() * JOGGING_SPEED)) * 6.0 * jogging;

    if (gun_pos > GUN_TARGET_POS) gun_pos -= 1;
    else if (gun_pos < GUN_TARGET_POS) gun_pos += 2;
    else if (!gun_fired && btnState[4] && player.ammo > 0) { 
      gun_pos = GUN_SHOT_POS;
      gun_fired = true;
      player.ammo--;
      fire();
    } else if (gun_fired && !btnState[4]) {
      gun_fired = false;
    }
  } else {
    display.setCursor(30, 20);
    display.setTextColor(WHITE);
    display.print(F("YOU DIED"));
    display.setCursor(10, 40);
    display.print(F("SURVIVED: ")); display.print(current_wave); display.print(F(" WAVES"));
    saveScore(16, hiDoom, current_wave);
    if (btnState[4]) {
      player.health = 100;
      player.ammo = 20;
      current_wave = 1;
      spawnWave(current_wave);
    }
    return;
  }

  if (abs(player.velocity) > 0.003) {
    updatePosition(player.x, player.y, player.dir_x * player.velocity * doom_delta, player.dir_y * player.velocity * doom_delta, false);
  } else player.velocity = 0;

  updateEntities();

  for (int x = 0; x < 128; x += 2) {
    double camera_x = 2.0 * x / 128.0 - 1.0;
    double ray_x = player.dir_x + player.plane_x * camera_x;
    double ray_y = player.dir_y + player.plane_y * camera_x;
    int map_x = (int)player.x; int map_y = (int)player.y;
    double delta_x = abs(1.0 / ray_x); double delta_y = abs(1.0 / ray_y);
    int step_x, step_y; double side_x, side_y;

    if (ray_x < 0) { step_x = -1; side_x = (player.x - map_x) * delta_x; } else { step_x = 1; side_x = (map_x + 1.0 - player.x) * delta_x; }
    if (ray_y < 0) { step_y = -1; side_y = (player.y - map_y) * delta_y; } else { step_y = 1; side_y = (map_y + 1.0 - player.y) * delta_y; }

    bool hit = false; int side = 0;
    while (!hit) {
      if (side_x < side_y) { side_x += delta_x; map_x += step_x; side = 0; } else { side_y += delta_y; map_y += step_y; side = 1; }
      if (getBlockAt(map_x, map_y) == E_WALL) hit = true;
    }

    double distance = (side == 0) ? (map_x - player.x + (1 - step_x) / 2.0) / ray_x : (map_y - player.y + (1 - step_y) / 2.0) / ray_y;
    if (distance < 0.1) distance = 0.1;
    zbuffer[x] = distance; zbuffer[x+1] = distance;

    int line_height = (int)(56 / distance);
    int draw_start = -line_height / 2 + 56 / 2 + (int)(view_height / distance);
    int draw_end = line_height / 2 + 56 / 2 + (int)(view_height / distance);
    if (draw_start < 0) draw_start = 0; if (draw_end >= 56) draw_end = 55;

    for (int y = draw_start; y <= draw_end; y++) {
      if(side == 1 && (y % 2 == 0)) continue; 
      doomPixel(x, y, 1); doomPixel(x+1, y, 1);
    }
  }

  for (int i = 0; i < num_entities - 1; i++) {
    for (int j = 0; j < num_entities - i - 1; j++) {
      if (entity[j].distance < entity[j + 1].distance) { Entity temp = entity[j]; entity[j] = entity[j + 1]; entity[j + 1] = temp; }
    }
  }
  for (int i = 0; i < num_entities; i++) {
    double tx, ty; translateIntoView(entity[i].x, entity[i].y, tx, ty);
    if (ty <= 0.1) continue;
    int sprite_screen_x = (int)(64 * (1.0 + tx / ty));
    int sprite_screen_y = 56 / 2 + (int)(view_height / ty);
    if (sprite_screen_x < -64 || sprite_screen_x > 128 + 64) continue;
    if (ty > zbuffer[max(0, min(127, sprite_screen_x))]) continue;

    if (entity[i].type == E_ENEMY) {
      int sprite = 0;
      if (entity[i].state == S_ALERT) sprite = (millis() / 500) % 2;
      else if (entity[i].state == S_FIRING) sprite = 2;
      else if (entity[i].state == S_HIT) sprite = 3;
      else if (entity[i].state == S_DEAD) sprite = entity[i].timer > 0 ? 3 : 4;
      doomSprite(sprite_screen_x - (int)(16.0 / ty), sprite_screen_y - (int)(8.0 / ty), bmp_imp_bits, bmp_imp_mask, 32, 32, sprite, ty);
      if (entity[i].state != S_DEAD) {
        int marker_y = sprite_screen_y - (int)(12.0 / ty) - 4;
        if (marker_y >= 0 && marker_y < 56) {
          doomPixel(sprite_screen_x, marker_y, 1); doomPixel(sprite_screen_x - 1, marker_y + 1, 1);
          doomPixel(sprite_screen_x + 1, marker_y + 1, 1); doomPixel(sprite_screen_x, marker_y + 2, 1);
        }
      }
    } 
    else if (entity[i].type == E_FIREBALL) doomSprite(sprite_screen_x - (int)(8.0 / ty), sprite_screen_y - (int)(8.0 / ty), bmp_fireball_bits, bmp_fireball_mask, 16, 16, 0, ty);
    else if (entity[i].type == E_MEDIKIT) doomSprite(sprite_screen_x - (int)(8.0 / ty), sprite_screen_y + (int)(5.0 / ty), bmp_items_bits, bmp_items_mask, 16, 16, 0, ty);
    else if (entity[i].type == E_AMMO) doomSprite(sprite_screen_x - (int)(8.0 / ty), sprite_screen_y + (int)(5.0 / ty), bmp_items_bits, bmp_items_mask, 16, 16, 1, ty);
  }

  int gx = 48 + sin(millis() * JOGGING_SPEED) * 10 * jogging;
  int gy = 56 - gun_pos + abs(cos(millis() * JOGGING_SPEED)) * 8 * jogging;
  if (gun_pos > GUN_SHOT_POS - 2) display.drawBitmap(gx + 6, gy - 11, bmp_fire_bits, 24, 20, 1);
  int clip_height = max(0, min(gy + 32, 56) - gy);
  display.drawBitmap(gx, gy, bmp_gun_mask, 32, clip_height, 0);
  display.drawBitmap(gx, gy, bmp_gun_bits, 32, clip_height, 1);

  display.setCursor(0, 57); display.setTextColor(1);
  display.print(F("HP:")); display.print(player.health);
  display.print(F(" AMMO:")); display.print(player.ammo);
  display.print(F(" W:")); display.print(current_wave);
  if (!enemies_spawned) {
    int time_left = 3 - (millis() - wave_start_time) / 1000;
    if (time_left > 0) { display.setCursor(20, 20); display.print(F("WAVE STARTS IN ")); display.print(time_left); }
  }

  if (flash_screen > 0) { invert_screen = !invert_screen; flash_screen--; } else invert_screen = false;
  display.invertDisplay(invert_screen);
}

void loopMenu() {
  if(bp(0) || bp(2)) { mSel = (mSel + NUM_APPS - 1) % NUM_APPS; }
  if(bp(1) || bp(3)) { mSel = (mSel + 1) % NUM_APPS; }
  
  targetMenuScrollY = mSel * 20 - 20; 
  if (targetMenuScrollY < 0) targetMenuScrollY = 0;
  if (targetMenuScrollY > (NUM_APPS * 20) - 60) targetMenuScrollY = (NUM_APPS * 20) - 60;
  targetMenuBoxY = mSel * 20 + 2; 

  if(bp(4)) {
    activeApp = (AppMode)mSel; targetCameraX = 64; 
    if(activeApp==M_CALC){ cClear(); cEng=false; cMaxRows=4; targetCalcX=3+cCol*15; targetCalcY=36+cRow*22; }
    else if(activeApp==M_RACER) racerInit(); 
    else if(activeApp==M_WIFI) {
      wSt=W_START_SCAN;
      if(!isWifiApStarted) {
        WiFi.softAP("ESP32-C3-Clock", ""); 
        server.on("/", handleRoot); server.on("/set", handleSetTime); server.begin();
        isWifiApStarted = true;
      }
    }
    else if(activeApp==M_TETRIS) tetrisInit();
    else if(activeApp==M_BLOCKS) blocksInit(); 
    else if(activeApp==M_SNAKE) snakeInit();
    else if(activeApp==M_BLE) {
      if(!isBleStarted) { 
        bleInit(); 
        isBleStarted = true; 
      }
    }
    else if(activeApp==M_DOOM) doomInit();
    else if(activeApp==M_NOTES) notesInit();
    else if(activeApp==M_MATH) mathInit();
  }
}

void loopTime() {} 

void loopCalc() {
  if(bp(0)) cRow=(cRow+cMaxRows-1)%cMaxRows; if(bp(1)) cRow=(cRow+1)%cMaxRows; if(bp(2)) cCol=(cCol+3)%4; if(bp(3)) cCol=(cCol+1)%4;
  if(bp(4)) cKey(cEng ? KEYS_ENG[cRow][cCol] : KEYS_NORM[cRow][cCol]); targetCalcX=3+cCol*15; targetCalcY=(cEng?26:36)+cRow*(cEng?19:22);
}

void loopRacer() {
  if (rGameOver) { if(bp(4)) racerInit(); return; }
  readADXL();
  rX -= (adxlX / 15.0); 
  if (rX < 5) rX = 5; if (rX > 49) rX = 49;
  
  for(byte i=0; i<3; i++) {
    rObs[i].y += rObs[i].v + (rScore * 0.15); 
    if (rObs[i].y > 130) { 
      float minY = 0;
      for(byte j=0; j<3; j++) { if (i != j && rObs[j].y < minY) minY = rObs[j].y; }
      rObs[i].y = minY - random(40, 70); rObs[i].x = random(10, 44); rObs[i].v = random(10, 20)/10.0; rScore++; 
    }
    if (rObs[i].y + 16 > 108 && rObs[i].y < 124) { if (abs(rX - rObs[i].x) < 8) { rGameOver = true; saveScore(0, hiRacer, rScore); } }
  }
}

void loopWifi() {
  if(wSt==W_START_SCAN){ WiFi.disconnect(); WiFi.scanNetworks(true); wSt=W_SCANNING; }
  else if(wSt==W_SCANNING){ int r=WiFi.scanComplete(); if(r>=0){ wCt=min(r,12); for(int i=0; i<wCt; i++){ wNet[i].ssid=WiFi.SSID(i); wNet[i].rssi=WiFi.RSSI(i); } WiFi.scanDelete(); wSel=0; targetWifiSY=0; wSt=W_LIST; } }
  else if(wSt==W_LIST){
    if(bp(0) || bp(2)) { if(wSel>0)wSel--; targetWifiSY=-wSel*20; } if(bp(1) || bp(3)) { if(wSel<wCt-1)wSel++; targetWifiSY=-wSel*20; }
    if(bp(4) && wCt>0) { wPwd[0]=0; vkR=0; vkC=0; targetVkX=5; targetVkY=40; wSt=W_KEYBOARD; }
  }
  else if(wSt==W_KEYBOARD){
    if(bp(0)) vkR=(vkR+3)%4; if(bp(1)) vkR=(vkR+1)%4; if(bp(2)) vkC=(vkC+2)%3; if(bp(3)) vkC=(vkC+1)%3; targetVkX=5+vkC*18; targetVkY=40+vkR*20;
    if(bp(4)){ char p=VK[vkR][vkC]; if(p=='<'){ int l=strlen(wPwd); if(l>0) wPwd[l-1]=0; } else if(p=='>'){ WiFi.begin(wNet[wSel].ssid.c_str(), wPwd); wTm = millis(); wSt = W_CONNECTING; connStep = 1; } else { int l=strlen(wPwd); if(l<14){ wPwd[l]=p; wPwd[l+1]=0; } } }
  }
  else if(wSt==W_CONNECTING){ 
    unsigned long elapsed = millis() - wTm; if (elapsed < 2500) connStep = 1; else connStep = 2; 
    if(WiFi.status() == WL_CONNECTED) { 
      configTzTime("MSK-3", "pool.ntp.org"); 
      wifiTimeSynced = true; 
      if (elapsed >= 4000) wSt = W_CONNECTED; 
    } else if (elapsed > 16000) { WiFi.disconnect(); wSt = W_FAILED; } 
  }
  else if(wSt==W_CONNECTED || wSt==W_FAILED){ if(bp(4)) wSt=W_START_SCAN; }
}

void loopTetris() {
  if(tDead) { if(bp(4)) tetrisInit(); return; }
  if(bp(2)) { if(!tCol(tX-1, tY, tRot)) tX--; } if(bp(3)) { if(!tCol(tX+1, tY, tRot)) tX++; } if(bp(0)) { byte nR=(tRot+1)%4; if(!tCol(tX, tY, nR)) tRot=nR; }
  int dropSpeed = btnState[1] ? 30 : max(100, 800 - tSc * 2);
  if(millis() - tTk > dropSpeed) { tTk = millis(); if(!tCol(tX, tY+1, tRot)) tY++; else tMerge(); }
}

void loopBlocks() {
  if (bState == B_GAMEOVER) { if (bp(4)) blocksInit(); return; }
  if (bState == B_GENERATE) { bGenerateShapes(); if (!bGameOver) { bState = B_SELECT; for (byte i=0; i<3; i++) if (activeShapes[i] != -1) { selectedShapeIdx = i; break; } } return; }
  if (bState == B_SELECT) {
    if (bp(2)) { byte a = selectedShapeIdx; do { a = (a + 2) % 3; } while (activeShapes[a] == -1 && a != selectedShapeIdx); selectedShapeIdx = a; }
    if (bp(3)) { byte a = selectedShapeIdx; do { a = (a + 1) % 3; } while (activeShapes[a] == -1 && a != selectedShapeIdx); selectedShapeIdx = a; }
    if (bp(4)) { if (activeShapes[selectedShapeIdx] != -1) { bState = B_PLACE; blockX = 3; blockY = 3; } }
  } else if (bState == B_PLACE) {
    BlockShape s = SHAPES[activeShapes[selectedShapeIdx]];
    if (bp(0) && blockY > 0) blockY--; if (bp(1) && blockY < 8 - s.h) blockY++; if (bp(2) && blockX > 0) blockX--; if (bp(3) && blockX < 8 - s.w) blockX++; 
    if (bp(4)) { if (bCanPlace(blockX, blockY, activeShapes[selectedShapeIdx])) { for (byte r = 0; r < s.h; r++) for (byte c = 0; c < s.w; c++) if (s.mask & (1 << (r * 3 + c))) grid[blockY + r][blockX + c] = 1; activeShapes[selectedShapeIdx] = -1; bState = B_DESTROY; } }
  } else if (bState == B_DESTROY) { bCheckLines(); bool allEmpty = true; for (byte i = 0; i < 3; i++) if (activeShapes[i] != -1) allEmpty = false; if (allEmpty) bState = B_GENERATE; else { bState = B_SELECT; for (byte i=0; i<3; i++) if (activeShapes[i] != -1) { selectedShapeIdx = i; break; } if (bCheckGameOver()) { bGameOver = true; bState = B_GAMEOVER; saveScore(8, hiBlast, bScore); } } }
}

void loopSnake() {
  if(snkDead) { if(bp(4)) snakeInit(); return; }
  if(bp(2) && snkDX != 1) { snkDX = -1; snkDY = 0; } if(bp(3) && snkDX != -1) { snkDX = 1; snkDY = 0; }
  if(bp(0) && snkDY != 1) { snkDX = 0; snkDY = -1; } if(bp(1) && snkDY != -1) { snkDX = 0; snkDY = 1; }
  
  int speed = max(40, 250 - snkScore * 15);
  
  if(millis() - snkTk > speed) {
    snkTk = millis();
    int8_t nx = snk[0].x + snkDX, ny = snk[0].y + snkDY;
    if(nx < 0 || nx > 14 || ny < 0 || ny > 26) { snkDead = true; saveScore(12, hiSnake, snkScore); return; }
    for(int i=0; i<snkLen; i++) if(snk[i].x == nx && snk[i].y == ny) { snkDead = true; saveScore(12, hiSnake, snkScore); return; }
    
    if(nx == snkFX && ny == snkFY) { if(snkLen < 100) snkLen++; snkScore++; sSpawnFood(); }
    for(int i = snkLen-1; i > 0; i--) snk[i] = snk[i-1];
    snk[0].x = nx; snk[0].y = ny;
  }
}

void loopBLE() {
  if (!isBleConnected || millis() - lastBleTick < 8) return;
  lastBleTick = millis();
  readADXL();

  int8_t moveX = 0, moveY = 0, wheel = 0;
  if (abs(adxlX) > DEADZONE) moveX = -(adxlX / SENSITIVITY); 
  if (abs(adxlY) > DEADZONE) moveY = (adxlY / SENSITIVITY); 
  moveX = constrain(moveX, -127, 127); moveY = constrain(moveY, -127, 127);
  
  if (btnState[0]) wheel = 1;
  if (btnState[1]) wheel = -1;

  uint8_t currentButtons = 0; 
  if (btnState[4] && !selLongFired) currentButtons |= 1; 
  if (btnState[3]) currentButtons |= 2; 

  if (currentButtons != lastMouseButtons || moveX != 0 || moveY != 0 || wheel != 0) {
    uint8_t msg[] = {currentButtons, (uint8_t)moveX, (uint8_t)moveY, (uint8_t)wheel};
    if (input != nullptr) { input->setValue(msg, sizeof(msg)); input->notify(); }
    lastMouseButtons = currentButtons; mouseWasMoving = true;
  } else if (mouseWasMoving) {
    uint8_t stopMsg[] = {lastMouseButtons, 0, 0, 0};
    if (input != nullptr) { input->setValue(stopMsg, sizeof(stopMsg)); input->notify(); }
    mouseWasMoving = false;
  }
}

void loopADXL() {
  if (adxlSt == ADXL_MENU) {
    if (bp(0)) { if (adxlSel > 0) adxlSel--; }
    if (bp(1)) { if (adxlSel < 1) adxlSel++; }
    adxlSelAnimY = ease(adxlSelAnimY, adxlSel * 20, 0.4);
    
    if (bp(4)) {
      if (adxlSel == 0) adxlSt = ADXL_CUBE;
      else {
        adxlSt = ADXL_BALLS;
        shakeEnergy = 0; prev_raw_x = adxlX; prev_raw_y = adxlY;
        for(int i=0; i<NUM_BALLS; i++) { balls[i].vx = 0; balls[i].vy = 0; }
      }
    }
  } 
  else if (adxlSt == ADXL_CUBE) {
    if (bp(4)) adxlSt = ADXL_MENU;
    if (millis() - lastBleTick < 40) return;
    lastBleTick = millis(); readADXL(); 
  }
  else if (adxlSt == ADXL_BALLS) {
    if (bp(4)) adxlSt = ADXL_MENU;
    
    readADXL();
    int16_t jerk_x = adxlX - prev_raw_x; int16_t jerk_y = adxlY - prev_raw_y;
    prev_raw_x = adxlX; prev_raw_y = adxlY;

    float activity = sqrt(jerk_x * jerk_x + jerk_y * jerk_y);
    if (activity > 220.0f) { shakeEnergy += 1.8f; if (shakeEnergy > 100.0f) shakeEnergy = 100.0f; } 
    else { shakeEnergy *= 0.96f; }

    float shakeForceMultiplier = 1.0f + (shakeEnergy / 50.0f); 
    float currentRestitution = BASE_RESTITUTION + (shakeEnergy / 100.0f) * 0.22f;
    if (currentRestitution > 0.92f) currentRestitution = 0.92f;

    float ax = adxlX; float ay = adxlY; 
    if (SWAP_X_Y) { float temp = ax; ax = ay; ay = temp; }
    if (INVERT_X) ax = -ax; if (INVERT_Y) ay = -ay;

    if (abs((int)ax) < 85) ax = 0; if (abs((int)ay) < 85) ay = 0;

    float gx = ax * ACCEL_SCALE_X * shakeForceMultiplier;
    float gy = ay * ACCEL_SCALE_Y * shakeForceMultiplier;

    if (abs((int)ay) > 400) gy *= 1.8f; if (abs((int)ax) > 400) gx *= 1.8f;

    for (int i = 0; i < NUM_BALLS; i++) {
      balls[i].vx += gx; balls[i].vy += gy;
      balls[i].vx *= FRICTION; balls[i].vy *= FRICTION;
      
      if (abs(balls[i].vx) < 0.15f && abs(gx) < 0.1f) balls[i].vx = 0;
      if (abs(balls[i].vy) < 0.15f && abs(gy) < 0.1f) balls[i].vy = 0;

      balls[i].x += balls[i].vx; balls[i].y += balls[i].vy;
      
      if (balls[i].x < balls[i].radius) { balls[i].x = balls[i].radius; balls[i].vx = -balls[i].vx * currentRestitution; } 
      else if (balls[i].x > 64 - balls[i].radius) { balls[i].x = 64 - balls[i].radius; balls[i].vx = -balls[i].vx * currentRestitution; }
      
      if (balls[i].y < balls[i].radius) { balls[i].y = balls[i].radius; balls[i].vy = -balls[i].vy * currentRestitution; } 
      else if (balls[i].y > 128 - balls[i].radius) { balls[i].y = 128 - balls[i].radius; balls[i].vy = -balls[i].vy * currentRestitution; }
    }

    for (int i = 0; i < NUM_BALLS; i++) {
      for (int j = i + 1; j < NUM_BALLS; j++) {
        float dx = balls[j].x - balls[i].x; float dy = balls[j].y - balls[i].y;
        float distance = sqrt(dx * dx + dy * dy); float minDist = balls[i].radius + balls[j].radius;
        
        if (distance < minDist) {
          if (distance == 0.0f) { dx = 1.0f; dy = 0.0f; distance = 1.0f; }
          float overlap = minDist - distance; float nx = dx / distance; float ny = dy / distance;
          
          balls[i].x -= nx * (overlap * 0.5f); balls[i].y -= ny * (overlap * 0.5f);
          balls[j].x += nx * (overlap * 0.5f); balls[j].y += ny * (overlap * 0.5f);
          
          float rvx = balls[j].vx - balls[i].vx; float rvy = balls[j].vy - balls[i].vy;
          float velAlongNormal = rvx * nx + rvy * ny;
          
          if (velAlongNormal < 0) {
            float m1 = balls[i].mass; float m2 = balls[j].mass;
            float impulse = -(1.0f + currentRestitution) * velAlongNormal / (1.0f / m1 + 1.0f / m2);
            balls[i].vx -= (impulse / m1) * nx; balls[i].vy -= (impulse / m1) * ny;
            balls[j].vx += (impulse / m2) * nx; balls[j].vy += (impulse / m2) * ny;
            
            float tx = -ny; float ty = nx;
            float velAlongTangent = rvx * tx + rvy * ty;
            balls[i].vx += velAlongTangent * tx * 0.15f; balls[i].vy += velAlongTangent * ty * 0.15f;
            balls[j].vx -= velAlongTangent * tx * 0.15f; balls[j].vy -= velAlongTangent * ty * 0.15f;
          }
        }
      }
    }
  }
}

void loopDoom() {
  // Логика DOOM обрабатывается синхронно с рендером в runDoomFrame(),
  // поэтому здесь оставляем заглушку для корректной работы switch (activeApp).
}

void loopNotes() {
  int activeCount = 0; for (int i = 0; i < MAX_NOTES; i++) if (notes[i].inUse) activeCount++;
  
  if (nSt == N_LIST) {
    if (bp(0)) { if (nSel > 0) nSel--; }
    if (bp(1)) { if (nSel < activeCount && nSel < MAX_NOTES - (activeCount==MAX_NOTES?1:0)) nSel++; }
    targetNScrollY = nSel * 16 - 20; if(targetNScrollY < 0) targetNScrollY = 0;
    nScrollY = ease(nScrollY, targetNScrollY, 0.4);
    nSelAnimY = ease(nSelAnimY, nSel * 16, 0.4);
    
    if (bp(4)) {
      if (nSel == activeCount) { 
        strcpy(nKbdBuf, ""); nKbdX = 0; nKbdY = 0; nSt = N_EDIT_TITLE;
      } else { nActSel = 0; nSt = N_ACTION; }
    }
  }
  else if (nSt == N_ACTION) {
    if (bp(0)) { if (nActSel > 0) nActSel--; } if (bp(1)) { if (nActSel < 4) nActSel++; }
    if (bp(2)) { nSt = N_LIST; saveNotes(); }
    nActAnimY = ease(nActAnimY, nActSel * 13, 0.4);
    
    if (bp(4)) {
      if (nActSel == 0) { strcpy(nKbdBuf, notes[nSel].text); nKbdX = 0; nKbdY = 0; nSt = N_EDIT_TEXT; } 
      else if (nActSel == 1) { strcpy(nKbdBuf, notes[nSel].title); nKbdX = 0; nKbdY = 0; nSt = N_EDIT_TITLE; } 
      else if (nActSel == 2) { nLookScrollY = 0; nSt = N_LOOK; } 
      else if (nActSel == 3) { nActSel = 0; nSt = N_DEL; } 
      else if (nActSel == 4) { notes[nSel].isTicked = !notes[nSel].isTicked; nSt = N_LIST; saveNotes(); }
    }
  }
  else if (nSt == N_LOOK) {
    if (bp(0)) { if (nLookScrollY > 0) nLookScrollY -= 10; } if (bp(1)) { nLookScrollY += 10; }
    if (bp(2) || bp(4)) { nSt = N_ACTION; }
  }
  else if (nSt == N_DEL) {
    if (bp(2)) nActSel = 0; if (bp(3)) nActSel = 1;
    if (bp(4)) {
      if (nActSel == 1) { 
        notes[nSel].inUse = false;
        for (int i = nSel; i < MAX_NOTES - 1; i++) notes[i] = notes[i + 1];
        notes[MAX_NOTES - 1].inUse = false;
        if (nSel > 0) nSel--; saveNotes();
      }
      nSt = N_LIST;
    }
  }
  else if (nSt == N_EDIT_TITLE || nSt == N_EDIT_TEXT) {
    if (bp(0)) nKbdY = (nKbdY + 7) % 8; if (bp(1)) nKbdY = (nKbdY + 1) % 8;
    if (bp(2)) nKbdX = (nKbdX + 4) % 5; if (bp(3)) nKbdX = (nKbdX + 1) % 5;
    
    if (bp(4)) {
      char c = N_KBD_LAYOUT[nKbdY][nKbdX];
      int len = strlen(nKbdBuf);
      if (c == 'V') { 
        if (nSt == N_EDIT_TITLE) {
          strcpy(notes[nSel].title, len > 0 ? nKbdBuf : "note");
          strcpy(nKbdBuf, notes[nSel].text); nKbdX = 0; nKbdY = 0; nSt = N_EDIT_TEXT; 
        } else {
          strcpy(notes[nSel].text, nKbdBuf); notes[nSel].inUse = true;
          nSt = N_LIST; saveNotes(); 
        }
      } else if (c == 'X') { nSt = N_LIST; } 
      else if (c == '<') { if (len > 0) nKbdBuf[len - 1] = '\0'; } 
      else if (len < (nSt == N_EDIT_TEXT ? 31 : 11)) { nKbdBuf[len] = (c == '_') ? ' ' : c; nKbdBuf[len + 1] = '\0'; }
    }
  }
}

void loopMath() {
  if (mgSt == MG_PLAY) {
    if (millis() - mgStartTime >= 15000) { mgSt = MG_OVER; saveScore(20, hiMath, mgLevel); return; }
    if (bp(0)) { if (mgSel > 0) mgSel--; } if (bp(1)) { if (mgSel < 3) mgSel++; }
    mgSelAnimY = ease(mgSelAnimY, mgSel * 18, 0.4); 
    if (bp(4)) {
      if (mgOpts[mgSel] == mgAns) { mgLevel++; mathGenerate(); } 
      else { mgSt = MG_OVER; saveScore(20, hiMath, mgLevel); }
    }
  } else if (mgSt == MG_OVER || mgSt == MG_WIN) { if (bp(4)) { mgSt = MG_PLAY; mgLevel = 1; mathGenerate(); } }
}

unsigned long lastRender = 0;

void setup() {
  setCpuFrequencyMhz(160); // Максимальный разгон процессора ESP32-C3
  Serial.begin(115200); 
  Wire.begin(8, 9); 
  Wire.setClock(800000);   // Разгон шины I2C для быстрой передачи буфера OLED
  
  for (byte i=0; i<5; i++) pinMode(PINS[i], INPUT_PULLUP);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) while(1);
  
  display_buf = display.getBuffer(); 
  display.setRotation(3); 
  display.setTextWrap(false);
  
  EEPROM.begin(512); 
  loadScoresAndNotes(); 
  randomSeed(analogRead(2));

  adxlInit(); 
}

void loop() {
  updateClock(); 
  if(isWifiApStarted) server.handleClient(); 
  updateButtons(); 
  yield();

  if (selLongTriggeredNow) { 
    display.invertDisplay(false); 
    if (activeApp == M_BLOCKS && bState == B_PLACE) { bState = B_SELECT; } 
    else if (targetCameraX > 0) {
       if (activeApp == M_BLE && isBleConnected && input != nullptr) {
           uint8_t releaseMsg[] = {0, 0, 0, 0}; input->setValue(releaseMsg, sizeof(releaseMsg)); input->notify(); lastMouseButtons = 0;
       }
       if (activeApp == M_DOOM) {
           display.setRotation(3); 
           doomRunning = false;
       }
       targetCameraX = 0; 
       if (activeApp == M_WIFI) { for(int i=0; i<12; i++) wNet[i].ssid = ""; wCt = 0; wSt = W_START_SCAN; }
    }
  }

  if (targetCameraX == 0 && cameraX < 5.0) { loopMenu(); }
  else if (targetCameraX == 64 && cameraX > 59.0) {
    switch (activeApp) {
      case M_TIME: loopTime(); break; case M_CALC: loopCalc(); break; case M_RACER: loopRacer(); break;
      case M_WIFI: loopWifi(); break; case M_TETRIS: loopTetris(); break;
      case M_BLOCKS: loopBlocks(); break; case M_SNAKE: loopSnake(); break; case M_BLE: loopBLE(); break; 
      case M_ADXL: loopADXL(); break; 
      case M_DOOM: loopDoom(); break; 
      case M_NOTES: loopNotes(); break; case M_MATH: loopMath(); break;
    }
  }

  updateAnimations();

  // Строгий неблокирующий таймер для стабильных 60 FPS (16 мс)
  if (millis() - lastRender >= 16) {
    lastRender = millis(); 
    
    bool isDoomFull = (activeApp == M_DOOM && cameraX > 63.0);
    
    if (!isDoomFull) display.clearDisplay();

    if (cameraX < 63.0) drawMenu(-(int)cameraX);

    if (cameraX > 1.0) {
      int aOx = 64 - (int)cameraX; 
      switch (activeApp) {
        case M_TIME: drawTime(aOx); break; case M_CALC: drawCalc(aOx); break; case M_RACER: drawRacer(aOx); break;
        case M_WIFI: drawWifi(aOx); break; case M_TETRIS: drawTetris(aOx); break;
        case M_BLOCKS: drawBlocks(aOx); break; case M_SNAKE: drawSnake(aOx); break; case M_BLE: drawBLE(aOx); break;
        case M_ADXL: drawADXL(aOx); break; 
        case M_NOTES: drawNotes(aOx); break; case M_MATH: drawMath(aOx); break;
        case M_DOOM: 
          if (aOx == 0) {
            if (!doomRunning) {
              display.setRotation(0); 
              doomInit();
              doomRunning = true;
            }
            runDoomFrame();
          } else {
            display.setRotation(3); 
            printCenter(F("LOADING DOOM..."), aOx, 30, 64);
          }
          break;
      }
    }
    display.display(); // Отрисовка происходит всегда, без условий
  }
}
