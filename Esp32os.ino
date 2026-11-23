#include <Arduino.h>
#include <atomic>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <limits.h>
#include "RacerGame.h"
#include <esp_random.h>

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

struct WifiNet { char ssid[33]; };
struct BlockShape { byte w, h; uint16_t mask; };
struct SnakePt { int8_t x, y; };

struct Player { double x, y; double dir_x, dir_y; double plane_x, plane_y; double velocity; int health; int ammo; };
struct Entity { uint8_t type, state; double x, y; int health, timer; };

#define MAX_NOTES 5
enum NoteState { N_LIST, N_ACTION, N_EDIT_TITLE, N_EDIT_TEXT, N_DEL, N_LOOK };
struct Note { char title[12]; char text[32]; uint8_t isTicked; uint8_t inUse; };

enum MathState { MG_PLAY, MG_OVER };
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
void printCenter(const char* text, int x, int y, int bw);
void printCenter(const __FlashStringHelper* text, int x, int y, int bw);
void printRight(const char* text, int x, int y);
void updateToolAnimations();

// ─── 3. ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПИНЫ ────────────────────────────────────────────
constexpr int I2C_SDA = 8;
constexpr int I2C_SCL = 9;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr int APP_WIDTH = OLED_HEIGHT;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1, I2C_CLOCK_HZ, I2C_CLOCK_HZ);
uint8_t* display_buf = nullptr;
httpd_handle_t clockServer = nullptr;


const byte PINS[5] = {0, 1, 3, 4, 5}; 
AppMode activeApp = M_TIME;
byte mSel = 0;
const byte NUM_APPS = 12;
const char* const APP_NAMES[12] = {"TIME", "CALC", "RACER", "WI-FI", "TETRIS", "BLAST", "SNAKE", "MOUSE", "ADXL", "DOOM", "NOTES", "MATH"};

// ─── ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ПРИЛОЖЕНИЙ ────────────────────────────────────────
Note notes[MAX_NOTES];
NoteState nSt = N_LIST;
int8_t nSel = 0, nActSel = 0;
char nKbdBuf[32] = "", nDraftTitle[12] = "";
bool nEditingNew = false;
int8_t nKbdX = 0, nKbdY = 0;
float nActAnimY = 0;
float kbdAnimX = 0, kbdAnimY = 0, nSelAnimY = 0; 

const char N_KBD_LAYOUT[8][5] = {
  {'a','b','c','d','e'}, {'f','g','h','i','j'}, {'k','l','m','n','o'}, {'p','q','r','s','t'},
  {'u','v','w','x','y'}, {'z','0','1','2','3'}, {'4','5','6','7','8'}, {'9','_','<','V','X'}
};

MathState mgSt = MG_PLAY;
char mgOpStr[16] = "";
int mgAns, mgOpts[4];
int8_t mgSel = 0;
int mgLevel = 1;
uint32_t mgStartTime = 0;
float mgSelAnimY = 0;

// --- НАСТРОЙКИ ADXL BALLS ---
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
uint32_t adxlDemoTick = 0, adxlDemoPhase = 0;
bool adxlDemoSampleValid = false;
float cubePitch = 0, cubeRoll = 0;
const int NUM_BALLS = 5;
constexpr Ball initialBalls[NUM_BALLS] = {
  { 28.0, 20.0, 0.0, 0.0, 4.0, 16.0 }, { 40.0, 20.0, 0.0, 0.0, 5.0, 25.0 },
  { 52.0, 20.0, 0.0, 0.0, 6.0, 36.0 }, { 30.0, 40.0, 0.0, 0.0, 7.0, 49.0 },
  { 43.0, 45.0, 0.0, 0.0, 5.0, 25.0 }
};

Ball balls[NUM_BALLS];

int hiRacer=0, hiTetris=0, hiBlast=0, hiSnake=0, hiDoom=0, hiMath=0;


constexpr int StorageSize = 512;
constexpr int StorageMagicAddress = 500;
constexpr uint8_t StorageMagic = 0x47;
constexpr int NotesAddress = 30;
static_assert(sizeof(Note) == 46, "Keep the EEPROM note layout compatible");

bool storageReady = false;
bool storageDirty = false;
uint32_t storageAttemptAt = 0;

void queueStorageCommit() {
  if (!storageReady || storageDirty) return;
  storageDirty = true;
  storageAttemptAt = millis() + 500;
}

void loadScoresAndNotes() {
  storageReady = EEPROM.begin(StorageSize);
  if (!storageReady) {
    Serial.println(F("EEPROM unavailable"));
    return;
  }

  int* scores[] = { &hiRacer, &hiTetris, &hiBlast, &hiSnake, &hiDoom, &hiMath };
  const bool initialized = EEPROM.read(StorageMagicAddress) == StorageMagic;
  for (size_t i = 0; i < sizeof(scores) / sizeof(scores[0]); ++i) {
    const int address = i * sizeof(int);
    if (initialized) EEPROM.get(address, *scores[i]);
    if (*scores[i] < 0 || !initialized) {
      *scores[i] = 0;
      EEPROM.put(address, *scores[i]);
      queueStorageCommit();
    }
  }

  int activeNotes = 0;
  for (int i = 0; i < MAX_NOTES; ++i) {
    const int address = NotesAddress + i * sizeof(Note);
    if (initialized) {
      Note note;
      EEPROM.get(address, note);
      if (note.inUse != 1) continue;
      note.title[sizeof(note.title) - 1] = '\0';
      note.text[sizeof(note.text) - 1] = '\0';
      note.isTicked = note.isTicked == 1;
      notes[activeNotes++] = note;
    } else {
      EEPROM.put(address, notes[i]);
    }
  }
  if (!initialized) {
    EEPROM.write(StorageMagicAddress, StorageMagic);
    queueStorageCommit();
  }
}

void serviceStorage() {
  if (!storageDirty || static_cast<int32_t>(millis() - storageAttemptAt) < 0) return;
  if (EEPROM.commit()) {
    storageDirty = false;
  } else {
    storageAttemptAt = millis() + 5000;
    Serial.println(F("EEPROM commit failed"));
  }
}

void saveScore(int addr, int &hi, int score) {
  if (score <= hi) return;
  hi = score;
  if (storageReady) {
    EEPROM.put(addr, hi);
    queueStorageCommit();
  }
}

void saveNotes() {
  if (!storageReady) return;
  for (int i = 0; i < MAX_NOTES; ++i) EEPROM.put(NotesAddress + i * sizeof(Note), notes[i]);
  queueStorageCommit();
}

// ─── 4. BLUETOOTH MOUSE И АКСЕЛЕРОМЕТР ───────────────────────────────────────
BLECharacteristic* mouseInput = nullptr;
BLEAdvertising* mouseAdvertising = nullptr;
std::atomic<bool> mouseConnected{false};
std::atomic<bool> mouseDisconnected{false};
bool mouseAdvertisingPending = false;
uint8_t lastMouseButtons = 0;
uint32_t lastBleTick = 0;
uint32_t lastBleAdvertisingAttempt = 0;

const int DEADZONE = 4; 
const int SENSITIVITY = 2; 

const uint8_t hidReportMap[] = { 
  0x05,0x01,0x09,0x02,0xA1,0x01,0x85,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,
  0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,
  0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,
  0x95,0x03,0x81,0x06,0xC0,0xC0 
};

class BLECallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { mouseConnected.store(true); }
  void onDisconnect(BLEServer*) override {
    mouseConnected.store(false);
    mouseDisconnected.store(true);
  }
};

void enterBleApp() {
  if (mouseAdvertising == nullptr) {
    BLEDevice::init("Trxn MOUSE");
    BLEServer* mouseServer = BLEDevice::createServer();
    if (mouseServer == nullptr) {
      Serial.println(F("BLE mouse initialization failed"));
      return;
    }
    static BLECallbacks callbacks;
    mouseServer->setCallbacks(&callbacks);
    static BLEHIDDevice hid(mouseServer);
    mouseInput = hid.inputReport(1);
    hid.manufacturer()->setValue("Trxn OS");
    hid.pnp(0x02, 0xe502, 0xa111, 0x0210);
    hid.hidInfo(0x00, 0x01);
    static BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_BOND);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    hid.reportMap(const_cast<uint8_t*>(hidReportMap), sizeof(hidReportMap));
    hid.setBatteryLevel(100);
    hid.startServices();
    mouseAdvertising = mouseServer->getAdvertising();
    mouseAdvertising->setAppearance(HID_MOUSE);
    mouseAdvertising->addServiceUUID(hid.hidService()->getUUID());
  }
  lastMouseButtons = 0;
  lastBleTick = millis();
  mouseDisconnected.store(false);
  lastBleAdvertisingAttempt = lastBleTick;
  mouseAdvertisingPending = !mouseConnected.load() && !mouseAdvertising->start();
}

void exitBleApp() {
  if (mouseAdvertising == nullptr) return;
  if (mouseConnected.load()) {
    uint8_t release[] = {0, 0, 0, 0};
    mouseInput->setValue(release, sizeof(release));
    mouseInput->notify();
  } else {
    mouseAdvertising->stop();
  }
  lastMouseButtons = 0;
  mouseAdvertisingPending = false;
}

constexpr uint8_t ADXL345_ADDR = 0x53;
int16_t adxlX = 0, adxlY = 0, adxlZ = 0;
bool i2cReady = false;
bool adxlConfigured = false;
bool adxlRetryPending = false;
uint32_t adxlFailedAt = 0;

bool readAdxlRegisters(uint8_t address, uint8_t* data, size_t size) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(address);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint8_t>(ADXL345_ADDR), size, true) != size) return false;
  for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

bool writeAdxlRegister(uint8_t address, uint8_t value) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(address);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool configureADXL() {
  uint8_t deviceId = 0;
  return readAdxlRegisters(0x00, &deviceId, 1) && deviceId == 0xE5 &&
         writeAdxlRegister(0x2D, 0x00) &&  // Standby for configuration.
         writeAdxlRegister(0x31, 0x0B) &&  // Full resolution, +/-16 g.
         writeAdxlRegister(0x2C, 0x0A) &&  // 100 Hz output data rate.
         writeAdxlRegister(0x2D, 0x08);    // Measurement mode.
}

bool readADXL() {
  constexpr uint32_t ADXL_RETRY_MS = 250;
  const uint32_t now = millis();
  if (!i2cReady || (adxlRetryPending && now - adxlFailedAt < ADXL_RETRY_MS)) return false;

  if (!adxlConfigured) adxlConfigured = configureADXL();
  uint8_t interruptSource = 0;
  uint8_t data[6];
  const bool statusRead = adxlConfigured && readAdxlRegisters(0x30, &interruptSource, 1);
  if (statusRead && (interruptSource & 0x80) == 0) {
    adxlRetryPending = false;
    return false;
  }
  if (!statusRead || !readAdxlRegisters(0x32, data, sizeof(data))) {
    adxlConfigured = false;
    adxlRetryPending = true;
    adxlFailedAt = millis();
    return false;
  }
  adxlRetryPending = false;
  adxlX = static_cast<int16_t>(data[0] | (data[1] << 8));
  adxlY = static_cast<int16_t>(data[2] | (data[3] << 8));
  adxlZ = static_cast<int16_t>(data[4] | (data[5] << 8));
  return true;
}

bool btnState[5] = {}, btnJustPressed[5] = {}, btnJustReleased[5] = {};
bool rawBtnState[5] = {};
uint32_t rawBtnChangedAt[5] = {};
uint32_t selectPressedAt = 0;
bool selLongFired = false, selLongTriggeredNow = false;

bool initHardware() {
  if (!setCpuFrequencyMhz(160)) Serial.println(F("Unable to select the 160 MHz CPU setting"));
  randomSeed(esp_random());
  for (byte pin : PINS) pinMode(pin, INPUT_PULLUP);

  i2cReady = Wire.begin(I2C_SDA, I2C_SCL, I2C_CLOCK_HZ);
  if (!i2cReady) {
    Serial.println(F("I2C initialization failed"));
    return false;
  }
  Wire.setTimeOut(10);
  Wire.beginTransmission(OLED_ADDRESS);
  if (Wire.endTransmission() != 0 ||
      !display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, false, false)) {
    Serial.println(F("SSD1306 initialization failed"));
    return false;
  }
  display_buf = display.getBuffer();
  display.setRotation(3);
  display.setTextWrap(false);
  return true;
}

void updateButtons() {
  constexpr uint32_t DEBOUNCE_MS = 30;
  constexpr uint32_t SELECT_HOLD_MS = 600;
  const uint32_t now = millis();
  selLongTriggeredNow = false;
  for (byte i = 0; i < 5; i++) {
    btnJustPressed[i] = false;
    btnJustReleased[i] = false;
    const bool raw = digitalRead(PINS[i]) == LOW;
    if (raw != rawBtnState[i]) {
      rawBtnState[i] = raw;
      rawBtnChangedAt[i] = now;
    }
    // During release debounce, measure the hold through the first released sample.
    if (i == 4 && btnState[i] && !selLongFired) {
      const uint32_t heldThrough = raw ? now : rawBtnChangedAt[i];
      if (heldThrough - selectPressedAt >= SELECT_HOLD_MS) {
        selLongFired = true;
        selLongTriggeredNow = true;
      }
    }
    if (raw == btnState[i] || now - rawBtnChangedAt[i] < DEBOUNCE_MS) continue;
    btnState[i] = raw;
    btnJustPressed[i] = raw;
    btnJustReleased[i] = !raw;
    if (i == 4 && raw) {
      selectPressedAt = rawBtnChangedAt[i];
      selLongFired = false;
    }
  }
}
bool bp(byte i) { if (i == 4) return (btnJustReleased[4] && !selLongFired); return btnJustPressed[i]; }


constexpr char ClockTimezone[] = "MSK-3";
constexpr int SecondsPerDay = 24 * 60 * 60;
int hr = 12, mn = 0, sc = 0;
bool wifiTimeSynced = false;
bool clockWasSet = false;
std::atomic<int> pendingClockSeconds{-1};

void updateClock() {
  static uint32_t lastTick = 0;
  static int secondsOfDay = 12 * 60 * 60;
  const uint32_t now = millis();
  const uint32_t elapsedSeconds = (now - lastTick) / 1000;
  if (elapsedSeconds == 0) return;
  lastTick += elapsedSeconds * 1000;

  if (wifiTimeSynced || clockWasSet) {
    const time_t timestamp = time(nullptr);
    struct tm local{};
    if (localtime_r(&timestamp, &local)) {
      secondsOfDay = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    }
  } else {
    secondsOfDay = (secondsOfDay + elapsedSeconds) % SecondsPerDay;
  }
  hr = secondsOfDay / 3600;
  mn = (secondsOfDay / 60) % 60;
  sc = secondsOfDay % 60;
}

float cameraX = 0, targetCameraX = 0, menuScrollY = 0, targetMenuScrollY = 0, menuBoxY = 2, targetMenuBoxY = 2;
float calcX = 3, calcY = 36, targetCalcX = 3, targetCalcY = 36, wifiSY = 0, targetWifiSY = 0, vkX = 5, vkY = 40, targetVkX = 5, targetVkY = 40;

float ease(float curr, float target, float spd) { 
  float diff = target - curr; 
  if (abs(diff) < 0.2) return target; 
  return curr + diff * spd; 
}

void updateAnimations() {
  cameraX = ease(cameraX, targetCameraX, 0.65f);
  menuScrollY = ease(menuScrollY, targetMenuScrollY, 0.4);
  menuBoxY = ease(menuBoxY, targetMenuBoxY, 0.45); 
  calcX = ease(calcX, targetCalcX, 0.45); calcY = ease(calcY, targetCalcY, 0.45);
  wifiSY = ease(wifiSY, targetWifiSY, 0.4); vkX = ease(vkX, targetVkX, 0.45); vkY = ease(vkY, targetVkY, 0.45);
  updateToolAnimations();
}

void printRight(const char* text, int x, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x - w, y);
  display.print(text);
}

void printCenter(const char* text, int x, int y, int bw) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x + (bw - w) / 2, y);
  display.print(text);
}

void printCenter(const __FlashStringHelper* text, int x, int y, int bw) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x + (bw - w) / 2, y);
  display.print(text);
}

// ─── КАЛЬКУЛЯТОР ─────────────────────────────────────────────────────────────
const char KEYS_NORM[4][4] = { {'7','8','9','/'}, {'4','5','6','*'}, {'1','2','3','-'}, {'C','0','=','+'} };
const char KEYS_ENG[5][4]  = { {'R','^','S','T'}, {'7','8','9','/'}, {'4','5','6','*'}, {'1','2','3','-'}, {'C','0','=','+'} };

char dv[16] = "0", op1[16] = "";
char oper = 0;
bool isNew = true, cRightReady = false;
byte cRow = 0, cCol = 0, cMaxRows = 4;
bool cEng = false;
double cValue = 0, cLeftValue = 0;

void cClear() {
  strcpy(dv, "0");
  op1[0] = 0;
  oper = 0;
  isNew = true;
  cRightReady = false;
  cValue = cLeftValue = 0;
  cRow = cCol = 0;
}

void cFormat(double value) {
  cValue = value;
  isNew = true;
  cRightReady = true;
  if (!isfinite(value)) {
    strcpy(dv, "ERR");
    oper = 0;
    return;
  }
  if (value == 0.0 || (fabs(value) >= 0.001 && fabs(value) < 100000.0)) {
    snprintf(dv, sizeof(dv), fabs(value) == trunc(fabs(value)) ? "%.0f" : "%.3f", value);
  } else {
    snprintf(dv, sizeof(dv), "%.3g", value);
  }
  if (strlen(dv) > 9) snprintf(dv, sizeof(dv), "%.2g", value);
}

double cNumber() {
  return isNew ? cValue : strtod(dv, nullptr);
}

void cCalc() {
  if (!oper || !cRightReady) return;
  const double rhs = cNumber();
  switch (oper) {
    case '+': cFormat(cLeftValue + rhs); break;
    case '-': cFormat(cLeftValue - rhs); break;
    case '*': cFormat(cLeftValue * rhs); break;
    case '/': cFormat(rhs == 0 ? NAN : cLeftValue / rhs); break;
  }
  oper = 0;
  cRightReady = false;
}

void cKey(char key) {
  if (key >= '0' && key <= '9') {
    if (isNew || strcmp(dv, "0") == 0) {
      dv[0] = key;
      dv[1] = 0;
      isNew = false;
    } else {
      const size_t length = strlen(dv);
      if (length < 12) {
        dv[length] = key;
        dv[length + 1] = 0;
      }
    }
    cRightReady = true;
  } else if (key == 'C') {
    cClear();
    cEng = false;
    cMaxRows = 4;
  } else if (key == '+' || key == '-' || key == '*' || key == '/') {
    if (!isfinite(cNumber())) return;
    if (oper && cRightReady) cCalc();
    if (!isfinite(cNumber())) return;
    strcpy(op1, dv);
    cLeftValue = cNumber();
    oper = key;
    isNew = true;
    cValue = cLeftValue;
    cRightReady = false;
  } else if (key == '=') {
    if (!cEng && !oper && !isNew && strcmp(dv, "1") == 0) {
      cClear();
      cEng = true;
      cMaxRows = 5;
    } else {
      cCalc();
    }
  } else {
    const double value = cNumber();
    if (!isfinite(value)) return;
    if (key == 'R') cFormat(value < 0 ? NAN : sqrt(value));
    else if (key == '^') cFormat(value * value);
    else if (key == 'S') cFormat(sin(value));
    else if (key == 'T') cFormat(cos(value));
  }
}


RacerGame racer;
bool racerEntering = false;

int racerPick(int limit) { return random(limit); }

void racerInit() {
  racer.reset(micros(), racerPick);
  racerEntering = true;
}


constexpr int MaxWifiNetworks = 12;
constexpr int WifiPasswordLimit = 64;
constexpr int WifiKeyRows = 5;
constexpr int WifiLetterPages = 10;
constexpr uint32_t WifiScanTimeoutMs = 15000;
constexpr uint32_t WifiConnectTimeoutMs = 16000;

AppWifiState wSt = W_START_SCAN;
WifiNet wNet[MaxWifiNetworks];
int wCt = 0, wSel = 0;
uint8_t vkR = 0, vkC = 0, wKeyPage = 0, wPasswordLength = 0;
char wPwd[WifiPasswordLimit + 1] = "";
bool wShowPassword = false;
bool wifiConnectPending = false;
bool clockApStarted = false;
bool ntpConfigured = false;
bool wifiWasConnected = false;
uint32_t wifiStateStarted = 0;

uint16_t tBrd[20]; int8_t tX = 3, tY = 0; byte tTy = 0, tRot = 0; unsigned long tTk = 0; int tSc = 0; bool tDead = false;
const uint16_t TET[7][4] = { {0x0F00, 0x2222, 0x0F00, 0x2222}, {0x44C0, 0x8E00, 0x6440, 0x0E20}, {0x4460, 0x0E80, 0xC440, 0x2E00}, {0xCC00, 0xCC00, 0xCC00, 0xCC00}, {0x06C0, 0x8C40, 0x6C00, 0x4620}, {0x0E40, 0x4C40, 0x4E00, 0x4640}, {0x0C60, 0x4C80, 0xC600, 0x2640} };
bool tCol(int nx, int ny, byte rotation) {
  uint16_t pattern = TET[tTy][rotation];
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      if (!(pattern & (1u << (15 - (row * 4 + col))))) continue;
      int x = nx + col, y = ny + row;
      if (x < 0 || x >= 10 || y >= 20) return true;
      if (y >= 0 && (tBrd[y] & (1u << x))) return true;
    }
  }
  return false;
}

void tFinish() {
  tDead = true;
  saveScore(4, hiTetris, tSc);
}

void tSpawn() {
  tX = 3;
  tY = 0;
  tTy = random(7);
  tRot = 0;
  if (tCol(tX, tY, tRot)) tFinish();
}

void tetrisInit() {
  memset(tBrd, 0, sizeof(tBrd));
  tSc = 0;
  tDead = false;
  tSpawn();
  tTk = millis();
}

void tMerge() {
  uint16_t pattern = TET[tTy][tRot];
  if (tCol(tX, tY, tRot)) {
    tFinish();
    return;
  }
  for (int row = 0; tY + row < 0 && row < 4; row++) {
    if (pattern & (0xF000u >> (row * 4))) {
      tFinish();
      return;
    }
  }
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      if (!(pattern & (1u << (15 - (row * 4 + col))))) continue;
      int x = tX + col, y = tY + row;
      tBrd[y] |= 1u << x;
    }
  }

  int target = 19;
  for (int row = 19; row >= 0; row--) {
    if (tBrd[row] == 0x03FF) tSc = min(tSc, INT_MAX - 10) + 10;
    else tBrd[target--] = tBrd[row];
  }
  while (target >= 0) tBrd[target--] = 0;
  tSpawn();
}

byte grid[8][8];
int8_t activeShapes[3] = {-1, -1, -1};
byte selectedShapeIdx = 0;
int8_t blockX = 3, blockY = 3;
int bScore = 0;
BlockState bState = B_GENERATE;
const BlockShape SHAPES[12] = { {1,1,1}, {2,1,3}, {1,2,9}, {3,1,7}, {1,3,73}, {2,2,27}, {3,3,511}, {2,2,11}, {2,3,201}, {3,3,457}, {3,2,23}, {3,2,51} };
constexpr int BLOCK_SHAPE_COUNT = sizeof(SHAPES) / sizeof(SHAPES[0]);

bool bCanPlace(int bx, int by, int idx) {
  if (idx < 0 || idx >= BLOCK_SHAPE_COUNT) return false;
  const BlockShape &shape = SHAPES[idx];
  if (bx < 0 || by < 0 || bx + shape.w > 8 || by + shape.h > 8) return false;
  for (byte row = 0; row < shape.h; row++) {
    for (byte col = 0; col < shape.w; col++) {
      if ((shape.mask & (1u << (row * 3 + col))) && grid[by + row][bx + col]) return false;
    }
  }
  return true;
}

bool bShapeFits(int idx) {
  const BlockShape &shape = SHAPES[idx];
  for (int y = 0; y <= 8 - shape.h; y++) {
    for (int x = 0; x <= 8 - shape.w; x++) {
      if (bCanPlace(x, y, idx)) return true;
    }
  }
  return false;
}

void bSelectNextShape() {
  bool hasShape = false;
  for (byte slot = 0; slot < 3; slot++) {
    int idx = activeShapes[slot];
    if (idx < 0) continue;
    hasShape = true;
    if (bShapeFits(idx)) {
      selectedShapeIdx = slot;
      bState = B_SELECT;
      return;
    }
  }
  if (hasShape) {
    bState = B_GAMEOVER;
    saveScore(8, hiBlast, bScore);
  } else {
    bState = B_GENERATE;
  }
}

void bCheckLines() {
  bool clearRow[8] = {}, clearCol[8] = {};
  int lines = 0;
  for (byte i = 0; i < 8; i++) {
    bool rowFull = true, colFull = true;
    for (byte j = 0; j < 8; j++) {
      rowFull &= grid[i][j] != 0;
      colFull &= grid[j][i] != 0;
    }
    clearRow[i] = rowFull;
    clearCol[i] = colFull;
    lines += rowFull + colFull;
  }
  for (byte row = 0; row < 8; row++) {
    for (byte col = 0; col < 8; col++) {
      if (clearRow[row] || clearCol[col]) grid[row][col] = 0;
    }
  }
  if (lines > 0) {
    int points = lines * 100 + (lines - 1) * 50;
    bScore = min(bScore, INT_MAX - points) + points;
  }
}

void blocksInit() {
  memset(grid, 0, sizeof(grid));
  for (byte slot = 0; slot < 3; slot++) activeShapes[slot] = -1;
  selectedShapeIdx = 0;
  blockX = blockY = 3;
  bScore = 0;
  bState = B_GENERATE;
}

constexpr int SNAKE_WIDTH = 15;
constexpr int SNAKE_HEIGHT = 27;
constexpr int SNAKE_CELLS = SNAKE_WIDTH * SNAKE_HEIGHT;
SnakePt snk[SNAKE_CELLS];
int snkLen = 4;
int8_t snkDX = 0, snkDY = -1;
int8_t snkNextDX = 0, snkNextDY = -1;
int8_t snkFX = 5, snkFY = 5;
bool snkTurnPending = false;
bool snkDead = false, snkWon = false;
unsigned long snkTk = 0;
int snkScore = 0;

void sFinish(bool won) {
  snkDead = true;
  snkWon = won;
  saveScore(12, hiSnake, snkScore);
}

void sSpawnFood() {
  if (snkLen >= SNAKE_CELLS) {
    sFinish(true);
    return;
  }

  bool occupied[SNAKE_CELLS] = {};
  for (int i = 0; i < snkLen; i++) occupied[snk[i].y * SNAKE_WIDTH + snk[i].x] = true;
  int empty = random(SNAKE_CELLS - snkLen);
  for (int cell = 0; cell < SNAKE_CELLS; cell++) {
    if (occupied[cell] || empty-- != 0) continue;
    snkFX = cell % SNAKE_WIDTH;
    snkFY = cell / SNAKE_WIDTH;
    return;
  }
}

void snakeInit() {
  snkLen = 4;
  snkScore = 0;
  snkDX = snkNextDX = 0;
  snkDY = snkNextDY = -1;
  snkDead = snkWon = snkTurnPending = false;
  for (int i = 0; i < snkLen; i++) snk[i] = {7, static_cast<int8_t>(15 + i)};
  sSpawnFood();
  snkTk = millis();
}


void notesInit() {
  nSt = N_LIST;
  nSel = nActSel = 0;
  nSelAnimY = nActAnimY = 0;
}

// ─── MATH GAME ───────────────────────────────────────────────────────────────
constexpr uint32_t MathQuestionMs = 15000;

void mathGenerate() {
  mgStartTime = millis();
  mgSel = 0;
  mgSelAnimY = 0;
  const int difficulty = min(mgLevel, 99);
  const int type = mgLevel <= 3 ? 0 : random(mgLevel <= 7 ? 2 : 3);
  if (type == 0) {
    int a = random(10, 10 + difficulty * 20);
    int b = random(10, 10 + difficulty * 20);
    if (random(2)) {
      mgAns = a + b;
      snprintf(mgOpStr, sizeof(mgOpStr), "%d+%d", a, b);
    } else {
      if (a < b) { const int temp = a; a = b; b = temp; }
      mgAns = a - b;
      snprintf(mgOpStr, sizeof(mgOpStr), "%d-%d", a, b);
    }
  } else if (type == 1) {
    if (random(2)) {
      const int a = random(3, 5 + difficulty);
      const int b = random(3, 5 + difficulty);
      mgAns = a * b;
      snprintf(mgOpStr, sizeof(mgOpStr), "%dx%d", a, b);
    } else {
      const int b = random(3, 10 + difficulty / 2);
      mgAns = random(3, 10 + difficulty / 2);
      snprintf(mgOpStr, sizeof(mgOpStr), "%d:%d", b * mgAns, b);
    }
  } else {
    const int b = random(2, 10);
    const int quotient = random(2, 10);
    if (random(2)) {
      const int c = random(10, 50);
      mgAns = quotient + c;
      snprintf(mgOpStr, sizeof(mgOpStr), "%d:%d+%d", b * quotient, b, c);
    } else {
      const int c = random(1, quotient);
      mgAns = quotient - c;
      snprintf(mgOpStr, sizeof(mgOpStr), "%d:%d-%d", b * quotient, b, c);
    }
  }

  const int firstOffset = random(1, 10);
  mgOpts[0] = mgAns;
  for (int i = 1; i < 4; ++i) {
    const int offset = (firstOffset + i - 2) % 9 + 1;
    mgOpts[i] = mgAns >= offset && random(2) ? mgAns - offset : mgAns + offset;
  }
  for (int i = 3; i > 0; --i) {
    const int j = random(i + 1);
    const int temp = mgOpts[i];
    mgOpts[i] = mgOpts[j];
    mgOpts[j] = temp;
  }
}

void mathInit() {
  mgSt = MG_PLAY;
  mgLevel = 1;
  mathGenerate();
}

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
bool flash_screen = false;
double doom_delta = 1.0;
uint32_t doomLastFrameTime = 0;
int gun_pos = 0;
bool doomSelectPending = false;
uint32_t wave_start_time = 0;
bool enemies_spawned = false;
constexpr int DOOM_WIDTH = 128;
constexpr int DOOM_VIEW_HEIGHT = 56;
double zbuffer[DOOM_WIDTH];
bool doomRunning = false;
uint8_t doomRoute[MAP_SIZE * MAP_SIZE];
int doomRouteTarget = -1;

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
double doomDistanceSquared(double x, double y, double targetX, double targetY) {
  double dx = targetX - x, dy = targetY - y;
  return dx * dx + dy * dy;
}

void doomPixel(int x, int y, bool color) {
  if (x < 0 || x >= DOOM_WIDTH || y < 0 || y >= DOOM_VIEW_HEIGHT) return;
  uint8_t &value = display_buf[x + (y / 8) * DOOM_WIDTH];
  uint8_t bit = 1u << (y & 7);
  if (color) value |= bit;
  else value &= ~bit;
}

double doomWallDistance(double x, double y, double rayX, double rayY, bool &hitY) {
  int tileX = (int)x, tileY = (int)y;
  int stepX = rayX < 0 ? -1 : 1;
  int stepY = rayY < 0 ? -1 : 1;
  double deltaX = rayX == 0 ? INFINITY : fabs(1.0 / rayX);
  double deltaY = rayY == 0 ? INFINITY : fabs(1.0 / rayY);
  double sideX = (rayX < 0 ? x - tileX : tileX + 1 - x) * deltaX;
  double sideY = (rayY < 0 ? y - tileY : tileY + 1 - y) * deltaY;
  for (int step = 0; step < MAP_SIZE * 2; step++) {
    hitY = sideY <= sideX;
    double distance;
    if (hitY) {
      tileY += stepY;
      distance = sideY;
      sideY += deltaY;
    } else {
      tileX += stepX;
      distance = sideX;
      sideX += deltaX;
    }
    if (getBlockAt(tileX, tileY) == E_WALL) return distance;
  }
  return INFINITY;
}

bool doomCanSeePlayer(const Entity &enemy, double distance) {
  if (distance == 0) return true;
  bool hitY;
  return doomWallDistance(enemy.x, enemy.y, (player.x - enemy.x) / distance,
                          (player.y - enemy.y) / distance, hitY) >= distance;
}

void doomRouteStep(double x, double y, double &dx, double &dy) {
  static const int8_t stepX[] = {1, -1, 0, 0};
  static const int8_t stepY[] = {0, 0, 1, -1};
  int target = (int)player.y * MAP_SIZE + (int)player.x;
  if (target != doomRouteTarget) {
    doomRouteTarget = target;
    memset(doomRoute, 0, sizeof(doomRoute));
    uint16_t queue[MAP_SIZE * MAP_SIZE];
    int head = 0, tail = 0;
    queue[tail++] = target;
    doomRoute[target] = 5;
    while (head < tail) {
      int cell = queue[head++];
      for (int dir = 0; dir < 4; dir++) {
        int nx = cell % MAP_SIZE + stepX[dir], ny = cell / MAP_SIZE + stepY[dir];
        if (getBlockAt(nx, ny) != E_FLOOR) continue;
        int next = ny * MAP_SIZE + nx;
        if (doomRoute[next]) continue;
        doomRoute[next] = (dir ^ 1) + 1;
        queue[tail++] = next;
      }
    }
  }

  int tileX = (int)x, tileY = (int)y;
  int dir = doomRoute[tileY * MAP_SIZE + tileX] - 1;
  if (dir >= 0 && dir < 4) {
    dx = tileX + stepX[dir] + 0.5 - x;
    dy = tileY + stepY[dir] + 0.5 - y;
  }
}

void doomSprite(int x, int y, const uint8_t bitmap[], const uint8_t mask[],
                int w, int h, int sprite, double depth) {
  int right = min(DOOM_WIDTH, x + (int)(w / depth));
  int bottom = min(DOOM_VIEW_HEIGHT, y + (int)(h / depth));
  int top = max(0, y);
  int stride = w / 8;
  int offset = stride * h * sprite;
  for (int px = max(0, x); px < right; px++) {
    if (depth >= zbuffer[px]) continue;
    int sx = (int)((px - x) * depth);
    uint8_t bit = 128u >> (sx % 8);
    int column = offset + sx / 8;
    for (int py = top; py < bottom; py++) {
      int sy = (int)((py - y) * depth);
      int index = column + sy * stride;
      if (pgm_read_byte(mask + index) & bit) doomPixel(px, py, pgm_read_byte(bitmap + index) & bit);
    }
  }
}

void spawnEntity(uint8_t type, double x, double y) {
  if (num_entities >= MAX_ENTITIES) return;
  entity[num_entities++] = {type, S_STAND, x, y, type == E_ENEMY ? 100 : 0, 0};
}

void spawnFireball(double x, double y) {
  if (num_entities >= MAX_ENTITIES) return;
  double angle = atan2(player.y - y, player.x - x) * 180.0 / PI;
  entity[num_entities++] = {E_FIREBALL, S_STAND, x, y, (int)angle, 0};
}

void removeEntity(int index) {
  entity[index] = entity[--num_entities];
}

bool doomSpawnAt(int x, int y, uint8_t type) {
  if (getBlockAt(x, y) != E_FLOOR) return false;
  double cx = x + 0.5, cy = y + 0.5;
  double distance = doomDistanceSquared(player.x, player.y, cx, cy);
  if (distance < (type == E_ENEMY ? 25.0 : 1.0)) return false;
  for (int i = 0; i < num_entities; i++) {
    if (entity[i].x == cx && entity[i].y == cy) return false;
  }
  spawnEntity(type, cx, cy);
  return true;
}

bool doomSpawnRandom(uint8_t type) {
  if (num_entities >= MAX_ENTITIES) return false;
  for (int attempt = 0; attempt < 24; attempt++) {
    if (doomSpawnAt(random(1, MAP_SIZE - 1), random(1, MAP_SIZE - 1), type)) return true;
  }

  // A bounded fallback also works when random attempts hit occupied cells.
  constexpr int width = MAP_SIZE - 2;
  int start = random(width * width);
  for (int i = 0; i < width * width; i++) {
    int tile = (start + i) % (width * width);
    if (doomSpawnAt(tile % width + 1, tile / width + 1, type)) return true;
  }
  return false;
}

void spawnWave(int wave) {
  num_entities = 0;
  player.x = player.y = MAP_SIZE / 2.0;
  player.velocity = 0;
  wave_start_time = millis();
  enemies_spawned = false;
  // Leave room for up to 30 enemies and their first projectiles.
  int itemPairs = min((MAX_ENTITIES - 30 - 8) / 2, 3 + wave / 2);
  for (int i = 0; i < itemPairs; i++) {
    doomSpawnRandom(E_AMMO);
    doomSpawnRandom(E_MEDIKIT);
  }
}

bool doomBlocked(double fromX, double fromY, double x, double y, bool wallsOnly) {
  if (getBlockAt((int)x, (int)y) == E_WALL) return true;
  if (wallsOnly) return false;
  for (int i = 0; i < num_entities; i++) {
    if (entity[i].type != E_ENEMY || entity[i].health <= 0) continue;
    double distance = doomDistanceSquared(x, y, entity[i].x, entity[i].y);
    if (distance < 0.04 &&
        distance < doomDistanceSquared(fromX, fromY, entity[i].x, entity[i].y)) return true;
  }
  return false;
}

bool updatePosition(double &x, double &y, double dx, double dy, bool wallsOnly) {
  bool blockedX = doomBlocked(x, y, x + dx, y, wallsOnly);
  if (!blockedX) x += dx;
  bool blockedY = doomBlocked(x, y, x, y + dy, wallsOnly);
  if (!blockedY) y += dy;
  return blockedX || blockedY;
}

void translateIntoView(double x, double y, double &tx, double &ty) {
  double dx = x - player.x, dy = y - player.y;
  tx = (player.dir_y * dx - player.dir_x * dy) / 0.66;
  ty = player.dir_x * dx + player.dir_y * dy;
}

void fire() {
  bool hitY;
  double wall = doomWallDistance(player.x, player.y, player.dir_x, player.dir_y, hitY);
  double nearest = min(15.0, wall);
  int target = -1;
  for (int i = 0; i < num_entities; i++) {
    if (entity[i].type != E_ENEMY || entity[i].health <= 0) continue;
    double tx, depth;
    translateIntoView(entity[i].x, entity[i].y, tx, depth);
    if (depth <= 0 || depth >= nearest || fabs(tx) > 0.55) continue;
    double range = sqrt(doomDistanceSquared(player.x, player.y, entity[i].x, entity[i].y));
    if (!doomCanSeePlayer(entity[i], range)) continue;
    nearest = depth;
    target = i;
  }
  if (target < 0) return;

  Entity &enemy = entity[target];
  enemy.health -= 40 - (int)nearest;
  enemy.state = S_HIT;
  enemy.timer = 4;
}

void updateEntities() {
  if (!enemies_spawned && millis() - wave_start_time >= 3000) {
    int count = current_wave >= 9 ? 30 : 2 + (int)pow(1.5, current_wave);
    for (int i = 0; i < count; i++) {
      if (!doomSpawnRandom(E_ENEMY)) break;
    }
    enemies_spawned = true;
  }

  bool waveHasEnemies = false;
  for (int i = 0; i < num_entities && player.health > 0; i++) {
    Entity &actor = entity[i];
    if (actor.timer > 0) actor.timer--;
    double distance = doomDistanceSquared(player.x, player.y, actor.x, actor.y);
    switch (actor.type) {
      case E_ENEMY: {
        if (actor.health <= 0) {
          if (actor.state != S_DEAD) {
            actor.state = S_DEAD;
            actor.timer = 6;
          } else if (actor.timer == 0) {
            removeEntity(i--);
            break;
          }
          waveHasEnemies = true;
          break;
        }

        waveHasEnemies = true;
        if (actor.state == S_HIT || actor.state == S_FIRING) {
          if (actor.timer == 0) {
            actor.state = S_ALERT;
            actor.timer = 40;
          }
          break;
        }

        double length = sqrt(distance);
        bool canSeePlayer = doomCanSeePlayer(actor, length);
        if (distance <= 36 && canSeePlayer) {
          if (actor.state != S_MELEE) {
            actor.state = S_MELEE;
            actor.timer = 10;
          } else if (actor.timer == 0) {
            player.health = max(0, player.health - min(15, 2 + min(7, current_wave) * 2));
            actor.timer = 14;
            flash_screen = true;
          }
        } else {
          if (actor.state != S_ALERT) {
            actor.state = S_ALERT;
            actor.timer = 20;
          }
          if (distance < 400 && actor.timer == 0 && canSeePlayer) {
            spawnFireball(actor.x, actor.y);
            actor.state = S_FIRING;
            actor.timer = 6;
          } else if (length > 0) {
            double dx = player.x - actor.x, dy = player.y - actor.y;
            if (!canSeePlayer) {
              doomRouteStep(actor.x, actor.y, dx, dy);
              length = sqrt(dx * dx + dy * dy);
            }
            if (length > 0) {
              double step = min(1.0, 0.042 * doom_delta / length);
              updatePosition(actor.x, actor.y, dx * step, dy * step, true);
            }
          }
        }
        break;
      }

      case E_FIREBALL:
        if (distance < 0.5625) {
          player.health = max(0, player.health - min(25, 5 + min(7, current_wave) * 3));
          flash_screen = true;
          removeEntity(i--);
        } else {
          double angle = actor.health * PI / 180.0;
          double step = 0.2 * doom_delta;
          if (updatePosition(actor.x, actor.y, cos(angle) * step, sin(angle) * step, true)) {
            removeEntity(i--);
          }
        }
        break;

      case E_MEDIKIT:
      case E_AMMO:
        if (distance < 0.5625 && (actor.type == E_AMMO || player.health < 100)) {
          if (actor.type == E_MEDIKIT) player.health = min(100, player.health + 50);
          else player.ammo = min(999, player.ammo + 15);
          flash_screen = true;
          removeEntity(i--);
        }
        break;
    }
  }

  if (enemies_spawned && !waveHasEnemies && player.health > 0) {
    if (current_wave < INT_MAX) current_wave++;
    player.health = min(100, player.health + 20);
    spawnWave(current_wave);
  }
}

void doomInit() {
  player = {};
  player.dir_x = 1.0;
  player.plane_y = -0.66;
  player.health = 100;
  player.ammo = 20;
  current_wave = 1;
  gun_pos = 0;
  doomSelectPending = false;
  doom_delta = 1.0;
  if (invert_screen) display.invertDisplay(false);
  invert_screen = flash_screen = false;
  spawnWave(current_wave);
  doomLastFrameTime = millis();
}


esp_err_t handleClockPage(httpd_req_t* request) {
  return httpd_resp_sendstr(request,
    "<!doctype html><html lang='en'><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 Clock</title><h1>ESP32 Clock</h1>"
    "<form action='/set'>"
    "<label>Hours <input type='number' name='h' min='0' max='23' required></label> "
    "<label>Minutes <input type='number' name='m' min='0' max='59' required></label> "
    "<label>Seconds <input type='number' name='s' min='0' max='59' required></label> "
    "<button>Set time</button></form></html>");
}

bool readClockField(const char* query, const char* key, int maximum, int& result) {
  char value[3];
  if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK || !value[0]) return false;
  result = 0;
  for (const char* digit = value; *digit; ++digit) {
    if (*digit < '0' || *digit > '9') return false;
    result = 10 * result + (*digit - '0');
  }
  return result <= maximum;
}

esp_err_t handleClockSet(httpd_req_t* request) {
  char query[32];
  int hours, minutes, seconds;
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !readClockField(query, "h", 23, hours) ||
      !readClockField(query, "m", 59, minutes) ||
      !readClockField(query, "s", 59, seconds)) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Enter a valid time in HH:MM:SS.");
  }
  pendingClockSeconds.store(hours * 3600 + minutes * 60 + seconds);
  return httpd_resp_sendstr(request, "<!doctype html><html><p>Time set.</p><a href='/'>Back</a></html>");
}

void startClockServer() {
  if (clockServer) return;
  if (!clockApStarted) clockApStarted = WiFi.softAP("ESP32-C3-Clock");
  if (!clockApStarted) return;

  // HTTP socket waits belong to the ESP-IDF server task, away from the UI loop.
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = tskIDLE_PRIORITY + 1;
  config.max_open_sockets = 2;
  config.max_uri_handlers = 2;
  config.recv_wait_timeout = 1;
  config.send_wait_timeout = 1;
  config.lru_purge_enable = true;
  if (httpd_start(&clockServer, &config) != ESP_OK) return;

  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = handleClockPage;
  httpd_uri_t set = root;
  set.uri = "/set";
  set.handler = handleClockSet;
  if (httpd_register_uri_handler(clockServer, &root) != ESP_OK ||
      httpd_register_uri_handler(clockServer, &set) != ESP_OK) {
    httpd_stop(clockServer);
    clockServer = nullptr;
  }
}

// The framebuffer always uses physical OLED coordinates, including in portrait mode.
constexpr uint8_t OLED_COLUMNS = OLED_WIDTH;
constexpr uint8_t OLED_PAGES = OLED_HEIGHT / 8;
constexpr uint8_t OLED_DATA_CHUNK = 31;  // Leave one byte for the I2C control byte.

static bool sendDisplayWindow(const uint8_t* frame, uint8_t left, uint8_t right,
                              uint8_t firstPage, uint8_t lastPage, bool resync) {
  const uint8_t window[] = {
    0x00, SSD1306_COLUMNADDR, left, right, SSD1306_PAGEADDR, firstPage, lastPage
  };
  Wire.beginTransmission(OLED_ADDRESS);
  if (resync) {
    // A failed command transfer may have left the parser waiting for arguments.
    // Two NOPs fill any pending slots before horizontal addressing is restored.
    const uint8_t reset[] = {0x00, 0xE3, 0xE3, SSD1306_MEMORYMODE, 0x00};
    Wire.write(reset, sizeof(reset));
    Wire.write(window + 1, sizeof(window) - 1);
  } else {
    Wire.write(window, sizeof(window));
  }
  if (Wire.endTransmission() != 0) return false;

  // The controller advances across columns, then across pages. Pack consecutive
  // bytes across page boundaries to minimize the number of I2C transactions.
  uint8_t packet[OLED_DATA_CHUNK + 1] = {0x40};
  size_t packetSize = 1;
  for (uint8_t page = firstPage; page <= lastPage; ++page) {
    for (uint8_t col = left; col <= right; ++col) {
      packet[packetSize++] = frame[page * OLED_COLUMNS + col];
      const bool endOfWindow = page == lastPage && col == right;
      if (packetSize == sizeof(packet) || endOfWindow) {
        Wire.beginTransmission(OLED_ADDRESS);
        Wire.write(packet, packetSize);
        if (Wire.endTransmission() != 0) return false;
        packetSize = 1;
      }
    }
  }
  return true;
}

static uint16_t displayWindowCost(uint16_t bytes) {
  // One address plus seven command bytes, then address and control for each packet.
  return 8 + bytes + 2 * ((bytes + OLED_DATA_CHUNK - 1) / OLED_DATA_CHUNK);
}

void presentDisplay() {
  static uint8_t previous[OLED_COLUMNS * OLED_PAGES];
  static bool previousValid = false;
  const uint8_t* frame = display.getBuffer();
  if (!previousValid) {
    previousValid = sendDisplayWindow(frame, 0, OLED_COLUMNS - 1, 0, OLED_PAGES - 1, true);
    if (previousValid) memcpy(previous, frame, sizeof(previous));
    return;
  }

  uint8_t left[OLED_PAGES], right[OLED_PAGES];
  uint8_t firstPage = OLED_PAGES, lastPage = 0, firstCol = OLED_COLUMNS, lastCol = 0;
  uint16_t pageCost = 0;
  for (uint8_t page = 0; page < OLED_PAGES; ++page) {
    const size_t offset = page * OLED_COLUMNS;
    left[page] = OLED_COLUMNS;
    if (memcmp(frame + offset, previous + offset, OLED_COLUMNS) == 0) continue;

    uint8_t start = 0, end = OLED_COLUMNS - 1;
    while (frame[offset + start] == previous[offset + start]) ++start;
    while (frame[offset + end] == previous[offset + end]) --end;
    left[page] = start;
    right[page] = end;
    if (page < firstPage) firstPage = page;
    lastPage = page;
    if (start < firstCol) firstCol = start;
    if (end > lastCol) lastCol = end;
    pageCost += displayWindowCost(end - start + 1);
  }
  if (firstPage == OLED_PAGES) return;

  const uint16_t windowBytes = (lastPage - firstPage + 1) * (lastCol - firstCol + 1);
  if (displayWindowCost(windowBytes) <= pageCost) {
    previousValid = sendDisplayWindow(frame, firstCol, lastCol, firstPage, lastPage, false);
  } else {
    for (uint8_t page = firstPage; page <= lastPage; ++page) {
      if (left[page] == OLED_COLUMNS) continue;
      if (!sendDisplayWindow(frame, left[page], right[page], page, page, false)) {
        previousValid = false;
        break;
      }
    }
  }
  // Any failed transfer may have updated only part of the panel. Retry a full
  // frame next time before accepting another delta against the shadow buffer.
  if (previousValid) memcpy(previous, frame, sizeof(previous));
}

void drawMenuIcon(int x, int y, byte i) {
  const int c = WHITE;
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
  display.setTextColor(WHITE);
  for (byte i = 0; i < NUM_APPS; i++) { 
    int y = 4 + i * 20 - (int)menuScrollY; 
    if (y > -20 && y < 128) {
      drawMenuIcon(ox + 6, y + 2, i);
      display.setCursor(ox + 20, y + 5); 
      display.print(APP_NAMES[i]);
    }
  }
  const int drawBoxY = (int)menuBoxY - (int)menuScrollY;
  if (drawBoxY > -20 && drawBoxY < 128) {
    display.fillRoundRect(ox + 2, drawBoxY, 60, 18, 3, SSD1306_INVERSE);
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
  char hm[12];
  snprintf(hm, sizeof(hm), "%02d:%02d", hr % 24, mn % 60);
  printCenter(hm, ox, 72, 64); display.setTextSize(1);
  printCenter(wifiTimeSynced ? F("WiFi Sync") : clockWasSet ? F("Manual") : F("No Sync"), ox, 96, 64);
}

void drawCalc(int ox) {
  display.drawRoundRect(ox+2, 2, 60, 22, 3, WHITE); display.setTextColor(WHITE); display.setCursor(ox+6, 5); 
  if (oper) { 
    const size_t operandLength = strlen(op1);
    if (operandLength > 7) display.write('<');
    display.print(op1 + (operandLength > 7 ? operandLength - 6 : 0));
    display.write(' ');
    if (oper == '*') display.print('x');
    else if (oper == '/') display.print(':');
    else display.print(oper); 
  } 
  const size_t resultLength = strlen(dv);
  char tail[10];
  if (resultLength > 9) {
    tail[0] = '<';
    memcpy(tail + 1, dv + resultLength - 8, 9);
  }
  printRight(resultLength > 9 ? tail : dv, ox + 58, 14);
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
        display.setTextColor(col);
        const char key[] = { k, '\0' };
        printCenter(key, x, y + (cEng ? 4 : 6), 14);
      }
  }
}

void drawCarSprite(int x, int y, bool isEnemy) {
  display.fillRoundRect(x, y, RacerGame::CarWidth, RacerGame::CarHeight, 2, BLACK);
  display.drawRoundRect(x, y, RacerGame::CarWidth, RacerGame::CarHeight, 2, WHITE);
  display.fillRect(x + 2, y + (isEnemy ? 2 : 10), 6, 4, WHITE);
  for (int axle = y + 2; axle <= y + 10; axle += 8) {
    display.fillRect(x - 1, axle, 2, 4, WHITE);
    display.fillRect(x + RacerGame::CarWidth - 1, axle, 2, 4, WHITE);
  }
}

void drawRacerScore(char label, int value, int x) {
  display.setCursor(x, 2);
  display.print(label);
  if (value >= 1000000000) {
    display.print(value / 1000000000);
    display.print('B');
  } else if (value >= 1000000) {
    display.print(value / 1000000);
    display.print('M');
  } else if (value >= 10000) {
    display.print(value / 1000);
    display.print('K');
  } else {
    display.print(value);
  }
}

void drawRacer(int ox) {
  display.drawLine(ox + RacerGame::RoadLeft, RacerGame::RoadTop,
                   ox + RacerGame::RoadLeft, RacerGame::RoadBottom, WHITE);
  display.drawLine(ox + RacerGame::RoadRight, RacerGame::RoadTop,
                   ox + RacerGame::RoadRight, RacerGame::RoadBottom, WHITE);
  for (int y = static_cast<int>(racer.roadOffset()) - RacerGame::StripeSpacing;
       y <= RacerGame::RoadBottom; y += RacerGame::StripeSpacing) {
    const int top = max(y, static_cast<int>(RacerGame::RoadTop));
    const int bottom = min(y + 10, static_cast<int>(RacerGame::RoadBottom));
    if (top <= bottom) display.drawFastVLine(ox + 32, top, bottom - top + 1, WHITE);
  }
  for (int i = 0; i < RacerGame::ObstacleCount; ++i) {
    const auto& car = racer.obstacle(i);
    if (car.y + RacerGame::CarHeight > RacerGame::RoadTop && car.y <= RacerGame::RoadBottom) {
      drawCarSprite(ox + car.x, static_cast<int>(car.y), true);
    }
  }
  drawCarSprite(ox + static_cast<int>(racer.playerX()), RacerGame::PlayerY, false);

  display.fillRect(ox, 0, 64, RacerGame::RoadTop, BLACK);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  drawRacerScore('S', racer.score(), ox + 1);
  drawRacerScore('H', hiRacer, ox + 33);

  if (racer.crashed()) {
    display.fillRoundRect(ox + 4, 40, 56, 42, 4, BLACK);
    display.drawRoundRect(ox + 4, 40, 56, 42, 4, WHITE);
    printCenter(F("CRASHED"), ox, 48, 64);
    printCenter(F("SEL RETRY"), ox, 66, 64);
  }
}


char wifiKey(byte row, byte col) {
  const int position = row * 3 + col;
  const int index = position == 10 ? 9 : position;  // The bottom center is the tenth key.
  if (wKeyPage == 0) return "1234567890"[index];
  const int ascii = 32 + (wKeyPage - 1) * 10 + index;
  return ascii <= 126 ? static_cast<char>(ascii) : '\0';
}

void drawWifi(int ox) {
  display.setTextColor(WHITE);
  if (wSt == W_SCANNING) {
    printCenter(F("SCAN"), ox, 4, 64);
    const int cx = ox + 32, cy = 64, radius = 24;
    const float angle = (millis() % 2100) * 0.003f;
    display.drawCircle(cx, cy, radius, WHITE);
    display.drawLine(cx, cy, cx + radius * cosf(angle), cy + radius * sinf(angle), WHITE);
  } else if (wSt == W_LIST) {
    printCenter(F("NETS"), ox, 4, 64);
    display.drawFastHLine(ox, 14, 64, WHITE);
    if (wCt == 0) printCenter(F("No signal"), ox, 40, 64);
    for (int i = 0; i < wCt; ++i) {
      const int y = 18 + i * 20 + static_cast<int>(wifiSY);
      if (y <= 10 || y >= 120) continue;
      const bool selected = wSel == i;
      if (selected) display.fillRoundRect(ox + 2, y, 60, 18, 3, WHITE);
      display.setTextColor(selected ? BLACK : WHITE);
      display.setCursor(ox + 5, y + 5);
      const char* ssid = wNet[i].ssid;
      const size_t length = strlen(ssid);
      const size_t period = length + 3;
      const size_t start = selected && length > 9 ? (millis() / 400) % period : 0;
      for (size_t n = 0; n < 9 && (length > 9 || n < length); ++n) {
        const size_t k = (start + n) % period;
        display.write(k < length ? ssid[k] : ' ');
      }
    }
  } else if (wSt == W_KEYBOARD) {
    display.drawRoundRect(ox + 2, 10, 60, 22, 4, WHITE);
    char visible[10];
    const size_t count = min(static_cast<size_t>(wPasswordLength), sizeof(visible) - 2);
    for (size_t i = 0; i < count; ++i) {
      visible[i] = wShowPassword ? wPwd[wPasswordLength - count + i] : '*';
    }
    visible[count] = (millis() / 350) % 2 == 0 ? '_' : ' ';
    visible[count + 1] = '\0';
    printRight(visible, ox + 58, 17);

    display.fillRoundRect(ox + static_cast<int>(vkX), static_cast<int>(vkY), 18, 15, 3, WHITE);
    for (byte row = 0; row < WifiKeyRows; ++row) {
      for (byte col = 0; col < 3; ++col) {
        const int x = ox + 3 + col * 20, y = 40 + row * 17;
        display.drawRoundRect(x, y, 18, 15, 3, WHITE);
        display.setTextColor(row == vkR && col == vkC ? BLACK : WHITE);
        char label[4] = {};
        if (row == 4) {
          if (col == 0) {
            label[0] = 'P';
            const int page = wKeyPage + 1;
            label[1] = page >= 10 ? '1' : '0' + page;
            if (page >= 10) label[2] = '0' + page - 10;
          } else {
            strcpy(label, col == 1 ? "Eye" : "Esc");
          }
        } else if (row == 3 && col != 1) {
          label[0] = col == 0 ? '<' : '>';
        } else {
          const char key = wifiKey(row, col);
          if (key == ' ') strcpy(label, "Sp");
          else label[0] = key;
        }
        printCenter(label, x, y + 4, 18);
      }
    }
  } else if (wSt == W_CONNECTING) {
    printCenter(F("Connecting"), ox, 12, 64);
    display.drawRoundRect(ox + 7, 56, 50, 6, 3, WHITE);
    const int progress = min(46UL, (millis() - wifiStateStarted) * 46UL / WifiConnectTimeoutMs);
    if (progress) display.drawFastHLine(ox + 9, 58, progress, WHITE);
  } else if (wSt == W_CONNECTED) {
    printCenter(F("Success!"), ox, 20, 64);
    const IPAddress ip = WiFi.localIP();
    for (int row = 0; row < 2; ++row) {
      char part[9];
      snprintf(part, sizeof(part), "%u.%u%s", static_cast<unsigned>(ip[row * 2]),
               static_cast<unsigned>(ip[row * 2 + 1]), row == 0 ? "." : "");
      printCenter(part, ox, 50 + row * 10, 64);
    }
  } else if (wSt == W_FAILED) {
    printCenter(F("Error"), ox, 36, 64);
    printCenter(F("SEL: retry"), ox, 50, 64);
  }
}

void drawTBlock(int x, int y) { display.fillRect(x, y, 5, 4, WHITE); }

static void drawGameScore(int x, int y, char label, int score) {
  const bool fullWidth = score >= 1000000000;
  display.setCursor(x + (fullWidth ? 0 : 2), y);
  display.print(label);
  if (score < 100000000) {
    display.print(':');
  } else if (fullWidth) {
    display.setCursor(x + 5, y);
  }
  display.print(score);
}

static void drawGameScores(int ox, int score, int highScore) {
  display.setTextColor(WHITE);
  const bool compact = score < 1000 && highScore < 1000;
  drawGameScore(ox, 2, 'S', score);
  drawGameScore(ox + (compact ? 30 : 0), compact ? 2 : 10, 'H', highScore);
}

void drawTetris(int ox) {
  display.drawRect(ox+1, 24, 62, 102, WHITE);
  drawGameScores(ox, tSc, hiTetris);
  for(int r=0; r<20; r++) for(int c=0; c<10; c++) if((tBrd[r] & (1<<c)) != 0) drawTBlock(ox+2+c*6, 25+r*5);
  if (tDead) {
    display.fillRoundRect(ox+4, 40, 56, 40, 4, BLACK);
    display.drawRoundRect(ox+4, 40, 56, 40, 4, WHITE);
    printCenter(F("OVER"), ox, 48, 64);
    printCenter(F(">> SEL"), ox, 66, 64);
    return;
  }
  uint16_t p = TET[tTy][tRot]; for(int i=0; i<4; i++) for(int j=0; j<4; j++) if((p & (1 << (15 - (i*4+j)))) != 0 && tY+i >= 0) drawTBlock(ox+2+(tX+j)*6, 25+(tY+i)*5);
}

void drawBlocks(int ox) {
  drawGameScores(ox, bScore, hiBlast);
  display.drawRect(ox + 11, 19, 42, 42, WHITE);
  for (byte r=0; r<8; r++) for (byte c=0; c<8; c++) { int cx = ox + 12 + c*5, cy = 20 + r*5; if (grid[r][c] != 0) display.fillRect(cx, cy, 4, 4, WHITE); }
  if (bState == B_PLACE) {
    const byte shape = activeShapes[selectedShapeIdx];
    const BlockShape& s = SHAPES[shape];
    const bool fits = bCanPlace(blockX, blockY, shape);
    for (byte r = 0; r < s.h; r++) {
      for (byte c = 0; c < s.w; c++) {
        if (!(s.mask & (1 << (r * 3 + c)))) continue;
        const int x = blockX + c, y = blockY + r;
        if (x < 0 || x >= 8 || y < 0 || y >= 8) continue;
        const int px = ox + 12 + x * 5, py = 20 + y * 5;
        if (fits) display.fillRect(px, py, 4, 4, WHITE);
        else display.drawRect(px, py, 4, 4, grid[y][x] ? BLACK : WHITE);
      }
    }
  }
  for (byte i=0; i<3; i++) {
    int sx = ox + 4 + i * 20, sy = 75;
    if (activeShapes[i] != -1) { BlockShape s = SHAPES[activeShapes[i]]; for (byte r = 0; r < s.h; r++) for (byte c = 0; c < s.w; c++) if (s.mask & (1 << (r * 3 + c))) { if (bState == B_SELECT && selectedShapeIdx == i) display.fillRect(sx + c*4, sy + r*4, 3, 3, WHITE); else display.drawRect(sx + c*4, sy + r*4, 3, 3, WHITE); } if (bState == B_SELECT && selectedShapeIdx == i) display.drawRoundRect(sx - 2, sy - 4, 16, 18, 2, WHITE); }
  }
  if (bState == B_GAMEOVER) { display.fillRoundRect(ox + 4, 40, 56, 40, 4, BLACK); display.drawRoundRect(ox + 4, 40, 56, 40, 4, WHITE); printCenter(F("OVER"), ox, 48, 64); printCenter(F(">> SEL"), ox, 66, 64); }
}

void drawSnake(int ox) {
  display.drawRect(ox+1, 19, 62, 109, WHITE);
  drawGameScores(ox, snkScore, hiSnake);

  if (!snkWon) {
    const int fx = ox + 2 + snkFX * 4, fy = 20 + snkFY * 4;
    display.drawPixel(fx + 2, fy, WHITE);
    display.fillRect(fx, fy + 1, 4, 2, WHITE);
  }
  
  for(int i=1; i<snkLen; i++) display.fillRect(ox + 2 + snk[i].x*4, 20 + snk[i].y*4, 3, 3, WHITE);
  
  int hx = ox + 2 + snk[0].x*4, hy = 20 + snk[0].y*4;
  display.fillRect(hx, hy, 4, snk[0].y == SNAKE_HEIGHT - 1 ? 3 : 4, WHITE);
  if(snkDX == 1) { display.drawPixel(hx+2, hy+1, BLACK); display.drawPixel(hx+2, hy+2, BLACK); } 
  else if(snkDX == -1) { display.drawPixel(hx+1, hy+1, BLACK); display.drawPixel(hx+1, hy+2, BLACK); } 
  else if(snkDY == -1) { display.drawPixel(hx+1, hy+1, BLACK); display.drawPixel(hx+2, hy+1, BLACK); } 
  else { display.drawPixel(hx+1, hy+2, BLACK); display.drawPixel(hx+2, hy+2, BLACK); } 
  if (snkDead) {
    display.fillRoundRect(ox+4, 40, 56, 40, 4, BLACK);
    display.drawRoundRect(ox+4, 40, 56, 40, 4, WHITE);
    printCenter(snkWon ? F("WIN") : F("OVER"), ox, 48, 64);
    printCenter(F(">> SEL"), ox, 66, 64);
  }
}

void drawBLE(int ox) {
  display.setTextColor(WHITE);
  printCenter(F("MOUSE"), ox, 4, 64);
  display.drawFastHLine(ox + 4, 14, 57, WHITE);
  if (mouseAdvertising == nullptr) {
    printCenter(F("BLE error"), ox, 24, 64);
  } else if (mouseConnected.load()) {
    printCenter(F("Connected!"), ox, 20, 64);
    display.drawRoundRect(ox + 24, 32, 16, 26, 6, WHITE);
    display.drawFastHLine(ox + 24, 42, 16, WHITE);
    display.drawFastVLine(ox + 32, 32, 11, WHITE);
    if (btnState[4] && !selLongFired) display.fillRoundRect(ox + 24, 32, 8, 10, 3, WHITE);
    if (btnState[3]) display.fillRoundRect(ox + 32, 32, 8, 10, 3, WHITE);
  } else {
    printCenter(F("Pairing..."), ox, 24, 64);
    display.drawRoundRect(ox + 7, 40, 50, 6, 3, WHITE);
    display.fillRoundRect(ox + 7, 40, (millis() / 20) % 50, 6, 3, WHITE);
  }
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
    if (!adxlDemoSampleValid) printCenter(F("No sensor"), ox, 34, 64);
    const int cx = ox + 32, cy = 74;
    const float pitchCos = cosf(cubePitch), pitchSin = sinf(cubePitch);
    const float rollCos = cosf(cubeRoll), rollSin = sinf(cubeRoll);
    constexpr float SIZE = 12.0f;
    static constexpr float verts[8][3] = { {-SIZE, -SIZE, -SIZE}, { SIZE, -SIZE, -SIZE}, { SIZE,  SIZE, -SIZE}, {-SIZE,  SIZE, -SIZE}, {-SIZE, -SIZE,  SIZE}, { SIZE, -SIZE,  SIZE}, { SIZE,  SIZE,  SIZE}, {-SIZE,  SIZE,  SIZE} };
    int proj[8][2];
    for(int i=0; i<8; i++) {
      float x = verts[i][0], y = verts[i][1], z = verts[i][2];
      const float xy = y * pitchCos - z * pitchSin, xz = y * pitchSin + z * pitchCos;
      y = xy; z = xz;
      const float yx = x * rollCos + z * rollSin, yz = -x * rollSin + z * rollCos;
      x = yx; z = yz;
      float dist = 40.0; float w = dist / (dist + z);
      proj[i][0] = cx + (int)(x * w * 1.5); proj[i][1] = cy + (int)(y * w * 1.5);
    }
    static constexpr uint8_t edges[12][2] = { {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4}, {0,4}, {1,5}, {2,6}, {3,7} };
    for(int i=0; i<12; i++) display.drawLine(proj[edges[i][0]][0], proj[edges[i][0]][1], proj[edges[i][1]][0], proj[edges[i][1]][1], WHITE);
    printCenter(F("3D CUBE"), ox, 116, 64);
  }
  else if (adxlSt == ADXL_BALLS) {
    for (int i = 0; i < NUM_BALLS; i++) display.fillCircle(ox + (int)balls[i].x, (int)balls[i].y, (int)balls[i].radius, WHITE);
  }
}

int noteCount() {
  int count = 0;
  while (count < MAX_NOTES && notes[count].inUse) ++count;
  return count;
}

const char* noteDisplayTitle() {
  static char title[11];
  memcpy(title, notes[nSel].title, sizeof(title) - 1);
  title[sizeof(title) - 1] = '\0';
  return title;
}

void drawNotes(int ox) {
  display.setTextColor(WHITE);
  if (nSt == N_LIST) {
    const int activeCount = noteCount();
    int selY = 16 + (int)nSelAnimY;
    if (selY > 12 && selY < 128) display.fillRoundRect(ox+2, selY-2, 60, 15, 2, WHITE);

    for (int i = 0; i <= activeCount; i++) {
      if (i == MAX_NOTES) break;
      int y = 16 + i * 16;
      if (y > 4 && y < 128) { 
        bool isSel = (nSel == i); display.setTextColor(isSel ? BLACK : WHITE);
        if (i < activeCount) {
          display.drawCircle(ox+7, y+4, 4, isSel ? BLACK : WHITE);
          if (notes[i].isTicked) {
            display.drawLine(ox+5, y+4, ox+7, y+6, isSel ? BLACK : WHITE);
            display.drawLine(ox+7, y+6, ox+10, y+2, isSel ? BLACK : WHITE);
          }
          display.setCursor(ox+14, y);
          for (int j = 0; j < 8 && notes[i].title[j]; ++j) display.write(notes[i].title[j]);
        } else { display.setCursor(ox+4, y); display.print(F("+ add note")); }
      }
    }
    display.fillRect(ox, 0, 64, 14, BLACK); display.setTextColor(WHITE);
    printCenter(F("NOTES"), ox, 2, 64); display.drawLine(ox+4, 12, ox+60, 12, WHITE);
  } 
  else if (nSt == N_ACTION) {
    printCenter(noteDisplayTitle(), ox, 2, 64); display.drawRoundRect(ox+2, 14, 60, 70, 3, WHITE);
    const char* acts[] = {"text", "title", "look", "delete", "tick"};
    display.fillRoundRect(ox+4, 18 + (int)nActAnimY, 56, 13, 2, WHITE);
    for (int i = 0; i < 5; i++) {
      display.setTextColor(nActSel == i ? BLACK : WHITE);
      display.setCursor(ox+6, 20 + i * 13); display.print(acts[i]);
    }
  }
  else if (nSt == N_LOOK) {
    display.drawRoundRect(ox+2, 2, 60, 124, 2, WHITE); printCenter(noteDisplayTitle(), ox, 6, 64); display.drawLine(ox+4, 16, ox+60, 16, WHITE);
    const char* text = notes[nSel].text;
    for (int i = 0; text[i]; ++i) {
      display.setCursor(ox + 6 + (i % 8) * 6, 20 + (i / 8) * 10);
      display.write(text[i]);
    }
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
    const int length = strlen(nKbdBuf);
    for (int i = 0; i <= length; ++i) {
      display.setCursor(ox + 3 + (i % 10) * 6, 4 + (i / 10) * 8);
      if (i < length) display.write(nKbdBuf[i]);
      else if ((millis() / 300) % 2 == 0) display.write('_');
    }
    
    for(int r=0; r<=8; r++) display.drawLine(ox+2, 36+r*11, ox+62, 36+r*11, WHITE);
    for(int c=0; c<=5; c++) display.drawLine(ox+2+c*12, 36, ox+2+c*12, 36+8*11, WHITE);

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
    const uint32_t elapsed = min(static_cast<uint32_t>(millis() - mgStartTime), MathQuestionMs);
    const int barW = elapsed * 56 / MathQuestionMs;
    
    display.drawRoundRect(ox+2, 2, 60, 6, 2, WHITE);
    if (barW) display.fillRect(ox + 4, 4, barW, 2, WHITE);
    
    display.setCursor(ox + 2, 12); display.print(F("L:"));
    if (mgLevel > 999) display.print(F("1k+")); else display.print(mgLevel);
    display.setCursor(ox + 32, 12); display.print(F("H:"));
    if (hiMath > 999) display.print(F("1k+")); else display.print(hiMath);
    
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
    char level[11];
    if (mgLevel > 999999) strcpy(level, "LVL 999K+");
    else snprintf(level, sizeof(level), "LVL %d", max(1, mgLevel));
    printCenter(level, ox, 56, 64);
  }
}

void runDoomFrame() {
  uint32_t now = millis();
  uint32_t elapsed = now - doomLastFrameTime;
  doom_delta = elapsed == 0 ? 1.0 : min(elapsed, uint32_t(66)) / 33.3;
  doomLastFrameTime = now;
  display.clearDisplay();

  if (player.health <= 0) {
    if (doomSelectPending) {
      doomInit();
    } else {
      display.setTextColor(WHITE);
      display.setCursor(40, 16);
      display.print(F("YOU DIED"));
      display.setCursor(10, 30);
      display.print(F("WAVE "));
      display.print(current_wave);
      display.setCursor(10, 44);
      display.print(F("SEL TO RESTART"));
      return;
    }
  }

  double targetSpeed = btnState[3] ? 0.2 : btnState[2] ? -0.2 : 0;
  player.velocity += (targetSpeed - player.velocity) * (targetSpeed ? 0.4 : 0.5);
  double jogging = fabs(player.velocity) * 5.0;
  double phase = now * JOGGING_SPEED;
  double viewHeight = fabs(sin(phase)) * 6.0 * jogging;

  int turn = btnState[0] - btnState[1];
  if (turn) {
    double angle = turn * 0.12 * doom_delta;
    double cosine = cos(angle), sine = sin(angle);
    double dirX = player.dir_x * cosine - player.dir_y * sine;
    player.dir_y = player.dir_x * sine + player.dir_y * cosine;
    player.dir_x = dirX;
    player.plane_x = 0.66 * player.dir_y;
    player.plane_y = -0.66 * player.dir_x;
  }

  if (fabs(player.velocity) > 0.003) {
    updatePosition(player.x, player.y, player.dir_x * player.velocity * doom_delta,
                   player.dir_y * player.velocity * doom_delta, false);
  } else {
    player.velocity = 0;
  }

  if (gun_pos > GUN_TARGET_POS) gun_pos--;
  else if (gun_pos < GUN_TARGET_POS) gun_pos = min(GUN_TARGET_POS, gun_pos + 2);
  if (doomSelectPending && gun_pos == GUN_TARGET_POS) {
    doomSelectPending = false;
    if (player.ammo > 0) {
      player.ammo--;
      gun_pos = GUN_SHOT_POS;
      fire();
    }
  }

  updateEntities();
  if (player.health <= 0) {
    doomSelectPending = false;
    saveScore(16, hiDoom, current_wave);
  }

  for (int x = 0; x < DOOM_WIDTH; x += 2) {
    double cameraX = 2.0 * (x + 0.5) / DOOM_WIDTH - 1.0;
    double rayX = player.dir_x + player.plane_x * cameraX;
    double rayY = player.dir_y + player.plane_y * cameraX;
    bool hitY = false;
    double distance = max(0.1, doomWallDistance(player.x, player.y, rayX, rayY, hitY));
    zbuffer[x] = zbuffer[x + 1] = distance;

    int halfHeight = (int)(DOOM_VIEW_HEIGHT / (2 * distance));
    int horizon = DOOM_VIEW_HEIGHT / 2 + (int)(viewHeight / distance);
    int top = max(0, horizon - halfHeight);
    int bottom = min(DOOM_VIEW_HEIGHT - 1, horizon + halfHeight);
    int stride = hitY ? 2 : 1;
    if (hitY && top % 2 == 0) top++;
    for (int y = top; y <= bottom; y += stride) {
      doomPixel(x, y, true);
      doomPixel(x + 1, y, true);
    }
  }

  // Sort only drawable indices by camera depth, leaving simulation order intact.
  uint8_t order[MAX_ENTITIES];
  double screenX[MAX_ENTITIES], depth[MAX_ENTITIES];
  int visible = 0;
  for (int i = 0; i < num_entities; i++) {
    double lateral;
    translateIntoView(entity[i].x, entity[i].y, lateral, depth[i]);
    if (depth[i] <= 0.1) continue;
    screenX[i] = (DOOM_WIDTH / 2) * (1.0 + lateral / depth[i]);
    double radius = 16.0 / depth[i];
    if (screenX[i] + radius < 0 || screenX[i] - radius >= DOOM_WIDTH) continue;

    int j = visible;
    while (j > 0 && depth[order[j - 1]] < depth[i]) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = i;
    visible++;
  }

  for (int k = 0; k < visible; k++) {
    int i = order[k];
    const Entity &actor = entity[i];
    double distance = depth[i];
    int sx = (int)screenX[i];
    int sy = DOOM_VIEW_HEIGHT / 2 + (int)(viewHeight / distance);
    switch (actor.type) {
      case E_ENEMY: {
        int sprite = 0;
        if (actor.state == S_ALERT) sprite = (now / 500) % 2;
        else if (actor.state == S_FIRING) sprite = 2;
        else if (actor.state == S_HIT || actor.state == S_DEAD) sprite = 3;
        doomSprite(sx - (int)(16.0 / distance), sy - (int)(8.0 / distance),
                   bmp_imp_bits, bmp_imp_mask, 32, 32, sprite, distance);
        if (actor.state != S_DEAD && sx >= 0 && sx < DOOM_WIDTH && distance < zbuffer[sx]) {
          int markerY = sy - (int)(12.0 / distance) - 4;
          doomPixel(sx, markerY, true);
          if (sx > 0 && distance < zbuffer[sx - 1]) doomPixel(sx - 1, markerY + 1, true);
          if (sx + 1 < DOOM_WIDTH && distance < zbuffer[sx + 1]) doomPixel(sx + 1, markerY + 1, true);
          doomPixel(sx, markerY + 2, true);
        }
        break;
      }
      case E_FIREBALL:
        doomSprite(sx - (int)(8.0 / distance), sy - (int)(8.0 / distance),
                   bmp_fireball_bits, bmp_fireball_mask, 16, 16, 0, distance);
        break;
      case E_MEDIKIT:
      case E_AMMO:
        doomSprite(sx - (int)(8.0 / distance), sy + (int)(5.0 / distance),
                   bmp_items_bits, bmp_items_mask, 16, 16, actor.type == E_AMMO, distance);
        break;
    }
  }

  int gx = 48 + sin(phase) * 10 * jogging;
  int gy = DOOM_VIEW_HEIGHT - gun_pos + fabs(cos(phase)) * 8 * jogging;
  if (gun_pos > GUN_SHOT_POS - 2) display.drawBitmap(gx + 6, gy - 11, bmp_fire_bits, 24, 20, WHITE);
  int clipHeight = max(0, min(32, DOOM_VIEW_HEIGHT - gy));
  display.drawBitmap(gx, gy, bmp_gun_mask, 32, clipHeight, BLACK);
  display.drawBitmap(gx, gy, bmp_gun_bits, 32, clipHeight, WHITE);

  display.setCursor(0, DOOM_VIEW_HEIGHT);
  display.setTextColor(WHITE);
  display.print(F("HP:"));
  display.print(player.health);
  display.print(F(" A:"));
  display.print(player.ammo);
  display.print(F(" W:"));
  display.print(current_wave);
  if (!enemies_spawned) {
    uint32_t countdown = now - wave_start_time;
    if (countdown < 3000) {
      display.setCursor(14, 20);
      display.print(F("WAVE STARTS IN "));
      display.print(3 - countdown / 1000);
    }
  }

  bool shouldInvert = flash_screen && player.health > 0;
  if (invert_screen != shouldInvert) display.invertDisplay(shouldInvert);
  invert_screen = shouldInvert;
  flash_screen = false;
}

void loopMenu() {
  if(bp(0) || bp(2)) { mSel = (mSel + NUM_APPS - 1) % NUM_APPS; }
  if(bp(1) || bp(3)) { mSel = (mSel + 1) % NUM_APPS; }
  
  targetMenuScrollY = mSel * 20 - 20; 
  if (targetMenuScrollY < 0) targetMenuScrollY = 0;
  if (targetMenuScrollY > (NUM_APPS * 20) - 60) targetMenuScrollY = (NUM_APPS * 20) - 60;
  targetMenuBoxY = mSel * 20 + 2; 

  if (bp(4)) {
    activeApp = static_cast<AppMode>(mSel);
    targetCameraX = APP_WIDTH;
    switch (activeApp) {
      case M_CALC:
        cClear();
        cEng = false;
        cMaxRows = 4;
        targetCalcX = 3 + cCol * 15;
        targetCalcY = 36 + cRow * 22;
        break;
      case M_RACER: racerInit(); break;
      case M_WIFI: enterWifiApp(); break;
      case M_TETRIS: tetrisInit(); break;
      case M_BLOCKS: blocksInit(); break;
      case M_SNAKE: snakeInit(); break;
      case M_BLE: enterBleApp(); break;
      case M_ADXL: enterAdxlApp(); break;
      case M_NOTES: notesInit(); break;
      case M_MATH: mathInit(); break;
      case M_TIME:
      case M_DOOM:
        break;
    }
  }
}

void loopTime() {} 


void loopCalc() {
  if (bp(0)) cRow = (cRow + cMaxRows - 1) % cMaxRows;
  if (bp(1)) cRow = (cRow + 1) % cMaxRows;
  if (bp(2)) cCol = (cCol + 3) % 4;
  if (bp(3)) cCol = (cCol + 1) % 4;
  if (bp(4)) cKey(cEng ? KEYS_ENG[cRow][cCol] : KEYS_NORM[cRow][cCol]);
  targetCalcX = 3 + cCol * 15;
  targetCalcY = (cEng ? 26 : 36) + cRow * (cEng ? 19 : 22);
}

void loopRacer() {
  if (racer.crashed()) {
    if (bp(4)) racerInit();
    return;
  }
  const uint32_t now = micros();
  if (racerEntering) {
    racer.startClock(now);
    racerEntering = false;
  }
  if (!racer.due(now)) return;

  float steering = 0;
  if (btnState[2] || btnState[3]) {
    steering = static_cast<int>(btnState[3]) - static_cast<int>(btnState[2]);
  } else if (readADXL()) {
    const int tilt = -static_cast<int>(adxlX);
    const int deadzone = 8;
    const int fullSteer = 80;
    if (abs(tilt) > deadzone) {
      const int magnitude = min(abs(tilt), fullSteer) - deadzone;
      steering = static_cast<float>(magnitude) / (fullSteer - deadzone);
      if (tilt < 0) steering = -steering;
    }
  }

  if (racer.advance(now, steering, racerPick)) saveScore(0, hiRacer, racer.score());
}


void enterWifiApp() {
  setenv("TZ", ClockTimezone, 1);
  tzset();
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  startClockServer();
  wSt = WiFi.status() == WL_CONNECTED ? W_CONNECTED : W_START_SCAN;
}

void leaveWifiApp() {
  if (wifiConnectPending) {
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();
    wifiConnectPending = false;
  }
  wPasswordLength = 0;
  memset(wPwd, 0, sizeof(wPwd));
  wCt = 0;
  wSt = W_START_SCAN;
}

void serviceNetwork() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wifiWasConnected) {
    configTzTime(ClockTimezone, "pool.ntp.org");
    ntpConfigured = true;
  }
  wifiWasConnected = connected;
  if (ntpConfigured && esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    wifiTimeSynced = true;
  }

  const int requested = pendingClockSeconds.exchange(-1);
  if (requested < 0) return;
  const time_t current = time(nullptr);
  struct tm local{};
  if (!localtime_r(&current, &local)) return;
  local.tm_hour = requested / 3600;
  local.tm_min = (requested / 60) % 60;
  local.tm_sec = requested % 60;
  local.tm_isdst = -1;
  struct timeval newTime{};
  newTime.tv_sec = mktime(&local);
  if (settimeofday(&newTime, nullptr) == 0) {
    clockWasSet = true;
    wifiTimeSynced = false;
    hr = local.tm_hour;
    mn = local.tm_min;
    sc = local.tm_sec;
  }
}

void loopWifi() {
  if (wSt == W_START_SCAN) {
    WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true);
    wifiStateStarted = millis();
    wSt = result == WIFI_SCAN_FAILED ? W_FAILED : W_SCANNING;
  } else if (wSt == W_SCANNING) {
    const int result = WiFi.scanComplete();
    if (result >= 0) {
      wCt = 0;
      for (int i = 0; i < result && wCt < MaxWifiNetworks; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        bool listed = false;
        for (int j = 0; j < wCt; ++j) listed |= ssid == wNet[j].ssid;
        if (!listed) ssid.toCharArray(wNet[wCt++].ssid, sizeof(WifiNet::ssid));
      }
      WiFi.scanDelete();
      wSel = 0;
      wifiSY = targetWifiSY = 0;
      wSt = W_LIST;
    } else if (result == WIFI_SCAN_FAILED || millis() - wifiStateStarted >= WifiScanTimeoutMs) {
      wSt = W_FAILED;
    }
  } else if (wSt == W_LIST) {
    if ((bp(0) || bp(2)) && wSel > 0) --wSel;
    if ((bp(1) || bp(3)) && wSel < wCt - 1) ++wSel;
    targetWifiSY = -max(0, wSel - 4) * 20;
    if (!bp(4)) return;
    if (wCt == 0) {
      wSt = W_START_SCAN;
    } else {
      wPasswordLength = wKeyPage = vkR = vkC = 0;
      wShowPassword = false;
      memset(wPwd, 0, sizeof(wPwd));
      vkX = targetVkX = 3;
      vkY = targetVkY = 40;
      wSt = W_KEYBOARD;
    }
  } else if (wSt == W_KEYBOARD) {
    if (bp(0)) vkR = (vkR + WifiKeyRows - 1) % WifiKeyRows;
    if (bp(1)) vkR = (vkR + 1) % WifiKeyRows;
    if (bp(2)) vkC = (vkC + 2) % 3;
    if (bp(3)) vkC = (vkC + 1) % 3;
    targetVkX = 3 + vkC * 20;
    targetVkY = 40 + vkR * 17;
    if (!bp(4)) return;
    if (vkR == 4) {
      if (vkC == 0) wKeyPage = (wKeyPage + 1) % (WifiLetterPages + 1);
      if (vkC == 1) wShowPassword = !wShowPassword;
      if (vkC == 2) {
        wPasswordLength = 0;
        memset(wPwd, 0, sizeof(wPwd));
        wSt = W_LIST;
      }
    } else if (vkR == 3 && vkC == 0) {
      if (wPasswordLength) wPwd[--wPasswordLength] = '\0';
    } else if (vkR == 3 && vkC == 2) {
      const wl_status_t status = WiFi.begin(wNet[wSel].ssid, wPwd);
      wPasswordLength = 0;
      memset(wPwd, 0, sizeof(wPwd));
      wifiStateStarted = millis();
      wifiConnectPending = status != WL_CONNECT_FAILED;
      wSt = wifiConnectPending ? W_CONNECTING : W_FAILED;
    } else {
      const char key = wifiKey(vkR, vkC);
      if (key && wPasswordLength < WifiPasswordLimit) {
        wPwd[wPasswordLength++] = key;
        wPwd[wPasswordLength] = '\0';
      }
    }
  } else if (wSt == W_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnectPending = false;
      wSt = W_CONNECTED;
    } else if (millis() - wifiStateStarted >= WifiConnectTimeoutMs) {
      WiFi.disconnect();
      wifiConnectPending = false;
      wSt = W_FAILED;
    }
  } else if ((wSt == W_CONNECTED || wSt == W_FAILED) && bp(4)) {
    wSt = W_START_SCAN;
  }
}

void loopTetris() {
  if (tDead) {
    if (bp(4)) tetrisInit();
    return;
  }
  if (bp(2) && !tCol(tX - 1, tY, tRot)) tX--;
  if (bp(3) && !tCol(tX + 1, tY, tRot)) tX++;
  if (bp(0)) {
    byte rotation = (tRot + 1) % 4;
    const int kicks[] = {0, -1, 1, -2, 2};
    for (int kick : kicks) {
      if (tCol(tX + kick, tY, rotation)) continue;
      tX += kick;
      tRot = rotation;
      break;
    }
  }
  unsigned long now = millis();
  unsigned long dropSpeed = btnState[1] ? 30 : 800 - min(350, tSc) * 2;
  if (now - tTk >= dropSpeed) {
    tTk = now;
    if (tCol(tX, tY + 1, tRot)) tMerge();
    else tY++;
  }
}

void loopBlocks() {
  switch (bState) {
    case B_GAMEOVER:
      if (bp(4)) blocksInit();
      break;
    case B_GENERATE:
      for (byte slot = 0; slot < 3; slot++) activeShapes[slot] = random(BLOCK_SHAPE_COUNT);
      bSelectNextShape();
      break;
    case B_SELECT: {
      int direction = bp(2) ? -1 : bp(3) ? 1 : 0;
      if (direction) {
        for (int step = 1; step < 3; step++) {
          byte slot = (selectedShapeIdx + step * direction + 3) % 3;
          if (activeShapes[slot] < 0) continue;
          selectedShapeIdx = slot;
          break;
        }
      }
      if (bp(4) && activeShapes[selectedShapeIdx] >= 0) {
        bState = B_PLACE;
        blockX = blockY = 3;
      }
      break;
    }
    case B_PLACE: {
      int idx = activeShapes[selectedShapeIdx];
      const BlockShape &shape = SHAPES[idx];
      if (bp(0) && blockY > 0) blockY--;
      if (bp(1) && blockY < 8 - shape.h) blockY++;
      if (bp(2) && blockX > 0) blockX--;
      if (bp(3) && blockX < 8 - shape.w) blockX++;
      if (!bp(4) || !bCanPlace(blockX, blockY, idx)) break;
      for (byte row = 0; row < shape.h; row++) {
        for (byte col = 0; col < shape.w; col++) {
          if (shape.mask & (1u << (row * 3 + col))) grid[blockY + row][blockX + col] = 1;
        }
      }
      activeShapes[selectedShapeIdx] = -1;
      bState = B_DESTROY;
      break;
    }
    case B_DESTROY:
      bCheckLines();
      bSelectNextShape();
      break;
  }
}

void loopSnake() {
  if (snkDead) {
    if (bp(4)) snakeInit();
    return;
  }
  if (!snkTurnPending) {
    if (snkDX == 0 && (bp(2) || bp(3))) {
      snkNextDX = bp(2) ? -1 : 1;
      snkNextDY = 0;
      snkTurnPending = true;
    } else if (snkDY == 0 && (bp(0) || bp(1))) {
      snkNextDX = 0;
      snkNextDY = bp(0) ? -1 : 1;
      snkTurnPending = true;
    }
  }

  unsigned long now = millis();
  unsigned long speed = max(40, 250 - snkScore * 15);
  if (now - snkTk < speed) return;
  snkTk = now;
  snkDX = snkNextDX;
  snkDY = snkNextDY;
  snkTurnPending = false;

  int nx = snk[0].x + snkDX, ny = snk[0].y + snkDY;
  if (nx < 0 || nx >= SNAKE_WIDTH || ny < 0 || ny >= SNAKE_HEIGHT) {
    sFinish(false);
    return;
  }

  bool grows = nx == snkFX && ny == snkFY;
  int collisionLength = grows ? snkLen : snkLen - 1;
  for (int i = 0; i < collisionLength; i++) {
    if (snk[i].x != nx || snk[i].y != ny) continue;
    sFinish(false);
    return;
  }

  if (grows) snkLen++;
  for (int i = snkLen - 1; i > 0; i--) snk[i] = snk[i - 1];
  snk[0] = {static_cast<int8_t>(nx), static_cast<int8_t>(ny)};
  if (grows) {
    snkScore++;
    sSpawnFood();
  }
}

void loopBLE() {
  if (mouseAdvertising == nullptr) return;
  const uint32_t now = millis();
  constexpr uint32_t ADVERTISING_RETRY_MS = 500;
  if (mouseDisconnected.exchange(false)) {
    lastMouseButtons = 0;
    mouseAdvertisingPending = true;
    lastBleAdvertisingAttempt = now - ADVERTISING_RETRY_MS;
  }
  if (!mouseConnected.load()) {
    if (mouseAdvertisingPending && now - lastBleAdvertisingAttempt >= ADVERTISING_RETRY_MS) {
      lastBleAdvertisingAttempt = now;
      mouseAdvertisingPending = !mouseAdvertising->start();
    }
    return;
  }
  if (now - lastBleTick < 10) return;
  lastBleTick = now;

  int moveX = 0, moveY = 0;
  int8_t wheel = 0;
  if (readADXL()) {
    if (abs(adxlX) > DEADZONE) moveX = constrain(-adxlX / SENSITIVITY, -127, 127);
    if (abs(adxlY) > DEADZONE) moveY = constrain(adxlY / SENSITIVITY, -127, 127);
  }
  
  if (btnState[0]) wheel = 1;
  if (btnState[1]) wheel = -1;

  uint8_t currentButtons = 0;
  if (btnState[4] && !selLongFired) currentButtons |= 1;
  if (btnState[3]) currentButtons |= 2;

  if (currentButtons != lastMouseButtons || moveX != 0 || moveY != 0 || wheel != 0) {
    uint8_t msg[] = {currentButtons, (uint8_t)moveX, (uint8_t)moveY, (uint8_t)wheel};
    mouseInput->setValue(msg, sizeof(msg));
    mouseInput->notify();
    lastMouseButtons = currentButtons;
  }
}


void enterAdxlApp() {
  adxlSt = ADXL_MENU;
  adxlSelAnimY = adxlSel * 20;
  adxlDemoSampleValid = false;
  cubePitch = cubeRoll = 0;
  shakeEnergy = 0;
  memcpy(balls, initialBalls, sizeof(balls));
}

void keepBallOnScreen(Ball& ball, float restitution) {
  const float x = constrain(ball.x, ball.radius, 63.0f - ball.radius);
  const float y = constrain(ball.y, ball.radius, 127.0f - ball.radius);
  if ((ball.x - x) * ball.vx > 0) ball.vx *= -restitution;
  if ((ball.y - y) * ball.vy > 0) ball.vy *= -restitution;
  ball.x = x;
  ball.y = y;
}

void updateAdxlBalls(float ax, float ay) {
  if (SWAP_X_Y) { const float oldX = ax; ax = ay; ay = oldX; }
  if (INVERT_X) ax = -ax;
  if (INVERT_Y) ay = -ay;
  if (fabsf(ax) < 85) ax = 0;
  if (fabsf(ay) < 85) ay = 0;

  const float shake = 1.0f + shakeEnergy / 50.0f;
  const float restitution = BASE_RESTITUTION + shakeEnergy * 0.0022f;
  const float gx = ax * ACCEL_SCALE_X * shake * (fabsf(ax) > 400 ? 1.8f : 1.0f);
  const float gy = ay * ACCEL_SCALE_Y * shake * (fabsf(ay) > 400 ? 1.8f : 1.0f);
  constexpr float MaxSpeed = 4.0f;
  for (Ball& ball : balls) {
    ball.vx = constrain((ball.vx + gx) * FRICTION, -MaxSpeed, MaxSpeed);
    ball.vy = constrain((ball.vy + gy) * FRICTION, -MaxSpeed, MaxSpeed);
    if (fabsf(ball.vx) < 0.15f && fabsf(gx) < 0.1f) ball.vx = 0;
    if (fabsf(ball.vy) < 0.15f && fabsf(gy) < 0.1f) ball.vy = 0;
    ball.x += ball.vx;
    ball.y += ball.vy;
    keepBallOnScreen(ball, restitution);
  }

  for (int i = 0; i < NUM_BALLS; ++i) {
    Ball& a = balls[i];
    for (int j = i + 1; j < NUM_BALLS; ++j) {
      Ball& b = balls[j];
      const float dx = b.x - a.x, dy = b.y - a.y;
      const float squaredDistance = dx * dx + dy * dy;
      const float minDistance = a.radius + b.radius;
      if (squaredDistance >= minDistance * minDistance) continue;

      const float distance = sqrtf(squaredDistance);
      const float nx = distance > 0 ? dx / distance : 1.0f;
      const float ny = distance > 0 ? dy / distance : 0.0f;
      const float separation = (minDistance - distance) * 0.5f;
      a.x -= nx * separation; a.y -= ny * separation;
      b.x += nx * separation; b.y += ny * separation;

      const float relativeX = b.vx - a.vx, relativeY = b.vy - a.vy;
      const float alongNormal = relativeX * nx + relativeY * ny;
      if (alongNormal < 0) {
        const float impulse = -(1.0f + restitution) * alongNormal / (1.0f / a.mass + 1.0f / b.mass);
        a.vx -= impulse * nx / a.mass; a.vy -= impulse * ny / a.mass;
        b.vx += impulse * nx / b.mass; b.vy += impulse * ny / b.mass;
        const float tangent = (-relativeX * ny + relativeY * nx) * 0.15f;
        a.vx -= tangent * ny; a.vy += tangent * nx;
        b.vx += tangent * ny; b.vy -= tangent * nx;
      }
      keepBallOnScreen(a, restitution);
      keepBallOnScreen(b, restitution);
    }
  }
}

void loopADXL() {
  if (adxlSt == ADXL_MENU) {
    if (bp(0)) adxlSel = 0;
    if (bp(1)) adxlSel = 1;
    if (bp(4)) {
      adxlSt = adxlSel == 0 ? ADXL_CUBE : ADXL_BALLS;
      adxlDemoTick = micros();
      adxlDemoPhase = 0;
      adxlDemoSampleValid = false;
      cubePitch = cubeRoll = shakeEnergy = 0;
      memcpy(balls, initialBalls, sizeof(balls));
    }
    return;
  }
  if (bp(4)) {
    adxlSt = ADXL_MENU;
    return;
  }

  const uint32_t now = micros();
  const uint32_t elapsed = min(static_cast<uint32_t>(now - adxlDemoTick), uint32_t{50000});
  adxlDemoTick = now;
  adxlDemoPhase += elapsed * 60;
  if (adxlDemoPhase < 1000000) return;

  const bool fresh = readADXL();
  if (fresh) {
    if (adxlDemoSampleValid && adxlSt == ADXL_BALLS) {
      const float dx = static_cast<float>(adxlX) - prev_raw_x;
      const float dy = static_cast<float>(adxlY) - prev_raw_y;
      if (dx * dx + dy * dy > 220.0f * 220.0f) shakeEnergy = min(100.0f, shakeEnergy + 1.8f);
    }
    adxlDemoSampleValid = true;
    prev_raw_x = adxlX;
    prev_raw_y = adxlY;
  }
  while (adxlDemoPhase >= 1000000) {
    adxlDemoPhase -= 1000000;
    if (adxlSt == ADXL_CUBE) {
      if (!fresh) continue;
      const float x = adxlX, y = adxlY, z = adxlZ;
      const float pitch = atan2f(y, sqrtf(x * x + z * z)) * 1.8f;
      const float roll = atan2f(-x, sqrtf(y * y + z * z)) * 1.8f;
      cubePitch += (pitch - cubePitch) * 0.15f;
      cubeRoll += (roll - cubeRoll) * 0.15f;
    } else {
      updateAdxlBalls(fresh ? adxlX : 0, fresh ? adxlY : 0);
      shakeEnergy *= 0.96f;
    }
  }
}

void loopDoom() {
  if (doomRunning && bp(4)) doomSelectPending = true;
}


void startNoteEdit(NoteState state, const char* contents) {
  strcpy(nKbdBuf, contents);
  nKbdX = nKbdY = 0;
  kbdAnimX = kbdAnimY = 0;
  nSt = state;
}

void loopNotes() {
  if (nSt == N_LIST) {
    const int count = noteCount();
    if (bp(0) && nSel > 0) --nSel;
    if (bp(1) && nSel < min(count, MAX_NOTES - 1)) ++nSel;
    if (bp(4)) {
      if (nSel == count) {
        nEditingNew = true;
        nDraftTitle[0] = '\0';
        startNoteEdit(N_EDIT_TITLE, "");
      } else {
        nActSel = 0;
        nActAnimY = 0;
        nSt = N_ACTION;
      }
    }
  } else if (nSt == N_ACTION) {
    if (bp(2)) { nSt = N_LIST; return; }
    if (bp(0) && nActSel > 0) --nActSel;
    if (bp(1) && nActSel < 4) ++nActSel;
    if (!bp(4)) return;

    nEditingNew = false;
    strcpy(nDraftTitle, notes[nSel].title);
    switch (nActSel) {
      case 0: startNoteEdit(N_EDIT_TEXT, notes[nSel].text); break;
      case 1: startNoteEdit(N_EDIT_TITLE, notes[nSel].title); break;
      case 2: nSt = N_LOOK; break;
      case 3: nActSel = 0; nSt = N_DEL; break;
      case 4:
        notes[nSel].isTicked = !notes[nSel].isTicked;
        saveNotes();
        nSt = N_LIST;
        break;
    }
  } else if (nSt == N_LOOK) {
    if (bp(2) || bp(4)) nSt = N_ACTION;
  } else if (nSt == N_DEL) {
    if (bp(2)) nActSel = 0;
    if (bp(3)) nActSel = 1;
    if (bp(4)) {
      if (nActSel == 1) {
        for (int i = nSel; i < MAX_NOTES - 1; ++i) notes[i] = notes[i + 1];
        notes[MAX_NOTES - 1] = {};
        if (nSel > 0) --nSel;
        saveNotes();
      }
      nSt = N_LIST;
    }
  } else {
    if (bp(0)) nKbdY = (nKbdY + 7) % 8;
    if (bp(1)) nKbdY = (nKbdY + 1) % 8;
    if (bp(2)) nKbdX = (nKbdX + 4) % 5;
    if (bp(3)) nKbdX = (nKbdX + 1) % 5;
    if (!bp(4)) return;

    const char key = N_KBD_LAYOUT[nKbdY][nKbdX];
    const size_t length = strlen(nKbdBuf);
    if (key == 'V') {
      if (nSt == N_EDIT_TITLE) {
        strcpy(nDraftTitle, length ? nKbdBuf : "note");
        startNoteEdit(N_EDIT_TEXT, nEditingNew ? "" : notes[nSel].text);
      } else {
        if (nEditingNew) notes[nSel] = {};
        strcpy(notes[nSel].title, nDraftTitle);
        strcpy(notes[nSel].text, nKbdBuf);
        notes[nSel].inUse = true;
        saveNotes();
        nSt = N_LIST;
      }
    } else if (key == 'X') {
      nSt = N_LIST;
    } else if (key == '<') {
      if (length) nKbdBuf[length - 1] = '\0';
    } else {
      const size_t capacity = nSt == N_EDIT_TEXT ? sizeof(nKbdBuf) : sizeof(nDraftTitle);
      if (length + 1 < capacity) {
        nKbdBuf[length] = key == '_' ? ' ' : key;
        nKbdBuf[length + 1] = '\0';
      }
    }
  }
}

void loopMath() {
  if (mgSt == MG_OVER) {
    if (bp(4)) mathInit();
    return;
  }

  const bool expired = millis() - mgStartTime >= MathQuestionMs;
  if (!expired) {
    if (bp(0) && mgSel > 0) --mgSel;
    if (bp(1) && mgSel < 3) ++mgSel;
    if (!bp(4)) return;
    if (mgOpts[mgSel] == mgAns) {
      if (mgLevel < INT_MAX) ++mgLevel;
      mathGenerate();
      return;
    }
  }
  mgSt = MG_OVER;
  saveScore(20, hiMath, mgLevel);
}

void updateToolAnimations() {
  if (activeApp == M_ADXL && adxlSt == ADXL_MENU) {
    adxlSelAnimY = ease(adxlSelAnimY, adxlSel * 20, 0.4f);
  } else if (activeApp == M_MATH) {
    mgSelAnimY = ease(mgSelAnimY, mgSel * 18, 0.4f);
  } else if (activeApp == M_NOTES) {
    nSelAnimY = ease(nSelAnimY, nSel * 16, 0.4f);
    nActAnimY = ease(nActAnimY, nActSel * 13, 0.4f);
    kbdAnimX = ease(kbdAnimX, nKbdX * 12, 0.4f);
    kbdAnimY = ease(kbdAnimY, nKbdY * 11, 0.4f);
  }
}

constexpr uint32_t DefaultFrameUs = 33333;
constexpr uint32_t RacerFrameUs = 16667;
uint32_t lastRender = 0;
bool displayReady = false;

void setup() {
  Serial.begin(115200);
  displayReady = initHardware();
  loadScoresAndNotes();
}

void leaveActiveApp() {
  if (activeApp == M_BLE) exitBleApp();
  if (activeApp == M_WIFI) leaveWifiApp();
  if (activeApp == M_DOOM && doomRunning) {
    display.invertDisplay(false);
    display.setRotation(3);
    doomRunning = false;
  }
  targetCameraX = 0;
}

void updateActiveApp() {
  if (selLongTriggeredNow && targetCameraX != 0) {
    if (activeApp == M_BLOCKS && bState == B_PLACE) {
      bState = B_SELECT;
    } else {
      leaveActiveApp();
    }
    return;
  }

  if (targetCameraX == 0 && cameraX == 0) {
    loopMenu();
    return;
  }
  if (targetCameraX != APP_WIDTH || cameraX != APP_WIDTH) return;

  switch (activeApp) {
    case M_TIME: loopTime(); break;
    case M_CALC: loopCalc(); break;
    case M_RACER: loopRacer(); break;
    case M_WIFI: loopWifi(); break;
    case M_TETRIS: loopTetris(); break;
    case M_BLOCKS: loopBlocks(); break;
    case M_SNAKE: loopSnake(); break;
    case M_BLE: loopBLE(); break;
    case M_ADXL: loopADXL(); break;
    case M_DOOM: loopDoom(); break;
    case M_NOTES: loopNotes(); break;
    case M_MATH: loopMath(); break;
  }
}

void renderFrame() {
  updateAnimations();
  if (!displayReady) return;

  if (activeApp == M_DOOM && targetCameraX == APP_WIDTH && cameraX == APP_WIDTH) {
    if (!doomRunning) {
      display.setRotation(0);
      doomInit();
      doomRunning = true;
    }
    runDoomFrame();
    presentDisplay();
    return;
  }

  display.clearDisplay();
  const int offset = static_cast<int>(cameraX);
  if (offset < APP_WIDTH) drawMenu(-offset);
  if (offset > 0) {
    const int appOffset = APP_WIDTH - offset;
    switch (activeApp) {
      case M_TIME: drawTime(appOffset); break;
      case M_CALC: drawCalc(appOffset); break;
      case M_RACER: drawRacer(appOffset); break;
      case M_WIFI: drawWifi(appOffset); break;
      case M_TETRIS: drawTetris(appOffset); break;
      case M_BLOCKS: drawBlocks(appOffset); break;
      case M_SNAKE: drawSnake(appOffset); break;
      case M_BLE: drawBLE(appOffset); break;
      case M_ADXL: drawADXL(appOffset); break;
      case M_DOOM:
        display.setTextColor(WHITE);
        printCenter(F("DOOM"), appOffset, 30, APP_WIDTH);
        break;
      case M_NOTES: drawNotes(appOffset); break;
      case M_MATH: drawMath(appOffset); break;
    }
  }
  presentDisplay();
}

void loop() {
  updateButtons();
  updateActiveApp();
  updateClock();
  serviceNetwork();
  serviceStorage();

  const uint32_t now = micros();
  const bool racerFullscreen = activeApp == M_RACER && cameraX == APP_WIDTH && targetCameraX == APP_WIDTH;
  const uint32_t interval = racerFullscreen ? RacerFrameUs : DefaultFrameUs;
  const uint32_t elapsed = now - lastRender;
  if (elapsed >= interval) {
    lastRender = elapsed < 2 * interval ? lastRender + interval : now;
    renderFrame();
  }

  delay(1);
}
