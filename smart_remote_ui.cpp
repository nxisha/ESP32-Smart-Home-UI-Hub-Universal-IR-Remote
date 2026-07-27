#include <SPI.h>
#include <TFT_eSPI.h>
#include <IRac.h>
#include <IRsend.h>
#include <BleKeyboard.h>
#include <WiFi.h>
#include <time.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>

TFT_eSPI tft = TFT_eSPI();

// --- Network & NTP Configuration ---
const char* ssid       = "YOUR_WIFI_NAME";
const char* password   = "YOUR_WIFI_PASSWORD";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // IST (UTC+5:30)
const int   daylightOffset_sec = 0;

// --- BLE Client / Syska Bulb Target ---
BLEAddress bulbAddress("11:22:33:44:55:66");
BLEClient*  pClient  = nullptr;
BLERemoteService* pRemoteService = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;

// Known Telink/Mesh UUIDs utilized by Syska firmware
BLEUUID serviceUUID("0000ffe5-0000-1000-8000-00805f9b34fb");
BLEUUID charUUID("0000ffe9-0000-1000-8000-00805f9b34fb");
bool bulbConnected = false;

// --- Hardware Assignments ---
const uint16_t kIrLed = 12; 
IRac ac(kIrLed); 
IRsend irsend(kIrLed); 

BleKeyboard bleKeyboard("ESP32 Universal", "Espressif", 100);

// --- Display Configuration ---
#define PANEL_INVERTED false

#define BG_COLOR      0x0000  
#define TEXT_COLOR    0xFFFF  
#define TEXT_DARK     0x0000  
#define DIM_GREY      0x39E7  

#define CARD_TV       0x07E0  
#define CARD_AC       0x051D  
#define CARD_LIGHT    0xFDC0  

#define REMOTE_BTN    0x2965  
#define PRESS_COLOR   0x07E0  
#define POWER_OFF_RED 0xF800  
#define POWER_ON_GRN  0x07E0  

// --- Application State ---
enum ScreenState { HOME, AC_SELECT, AC_SCREEN, TV_SCREEN, LIGHT_SCREEN, SCREENSAVER };
ScreenState currentState = HOME;

// AC Target State 
decode_type_t activeACProtocol = decode_type_t::VOLTAS; 
String activeACTitle = "NEHA'S AC";
bool acPower = false;
int acTemp = 24;
int acModeIndex = 0;
String acModes[4] = {"COOL", "DRY", "FAN", "AUTO"};
bool swingOn = false;
bool turboOn = false;
bool acLightOn = false;
int fanSpeed = 1;

// TV & Lighting State
bool tvPower = false;
bool lightOn = false;

// 16-bit RGB565 Palette
uint16_t presetColors[16] = {
  0xF800, 0xFB20, 0xFFE0, 0xBFE0,   
  0x07E0, 0x07EF, 0x07FF, 0x051D,   
  0x001F, 0x401F, 0x781F, 0xF81F,   
  0xFC1F, 0xF810, 0xFFFF, 0xFEDA    
};
int selectedColorIndex = 0;
uint8_t shadeValue = 128;      
uint8_t brightnessValue = 200; 

// Power Management
unsigned long lastTouchTime = 0;
const unsigned long SCREENSAVER_TIMEOUT = 30UL * 60UL * 1000UL;
unsigned long lastEyeChange = 0;
unsigned long lastClockUpdate = 0;
int eyeOffsetX = 0;
int eyeHeight  = 50;

// ============================================
// BLE CLIENT SUBSYSTEM
// ============================================
bool connectToBulb() {
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
  }
  
  if (!pClient->connect(bulbAddress)) return false;
  
  pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    pClient->disconnect();
    return false;
  }
  
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    pClient->disconnect();
    return false;
  }
  
  return true;
}

void sendSyskaState() {
  if (!bulbConnected) {
    bulbConnected = connectToBulb();
  }
  
  if (bulbConnected && pRemoteCharacteristic != nullptr) {
    // Reverse-engineered 7-byte Syska control frame
    // [0]: Magic Byte (0x56) | [1-3]: RGB | [4]: W | [5]: Frame Type | [6]: Terminator (0xAA)
    uint8_t payload[7] = {0x56, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xAA}; 
    
    if (lightOn) {
      uint8_t r, g, b;
      color565toRGB(presetColors[selectedColorIndex], r, g, b);
      
      float brightRatio = (float)brightnessValue / 255.0;
      payload[1] = (uint8_t)(r * brightRatio);
      payload[2] = (uint8_t)(g * brightRatio);
      payload[3] = (uint8_t)(b * brightRatio);
    } else {
      payload[1] = 0x00;
      payload[2] = 0x00;
      payload[3] = 0x00;
    }
    
    pRemoteCharacteristic->writeValue(payload, sizeof(payload));
  }
}

// ============================================
// MAIN SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32_Smart_Hub"); 
  
  tft.init();
  tft.invertDisplay(PANEL_INVERTED);
  tft.setRotation(0); 

  // TFT Touch calibration vector
  uint16_t calData[5] = { 447, 3169, 344, 3329, 2 };
  tft.setTouch(calData);
  randomSeed(analogRead(34)); 

  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.drawCentreString("CONNECTING WIFI...", 120, 140, 2);

  WiFi.begin(ssid, password);
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 15) {
    delay(500);
    wifi_attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }

  bleKeyboard.begin();
  irsend.begin(); 
  ac.next.protocol = decode_type_t::VOLTAS; 

  bulbConnected = connectToBulb();

  drawHomeScreen();
  lastTouchTime = millis();
}

// ============================================
// EVENT LOOP
// ============================================
void loop() {
  uint16_t t_x = 0, t_y = 0;
  bool touched = tft.getTouch(&t_x, &t_y);

  if (touched) lastTouchTime = millis();

  // Screensaver State Machine Handler
  if (currentState == SCREENSAVER) {
    updateScreensaverAnimation();
    if (touched) {
      currentState = HOME;
      drawHomeScreen();
      delay(300); // Blocking debounce to prevent bleed-through touch execution
    }
    delay(30);
    return;
  }

  if (!touched && (millis() - lastTouchTime > SCREENSAVER_TIMEOUT)) {
    currentState = SCREENSAVER;
    drawScreensaverBase();
    return;
  }

  if (!touched) return;

  // --- HOME SCREEN ---
  if (currentState == HOME) {
    if (inZone(t_x, t_y, 16, 16, 96, 136)) {          
      tft.fillRoundRect(16, 16, 96, 136, 10, PRESS_COLOR);
      delay(120);
      currentState = TV_SCREEN;
      drawTVScreen();
    }
    else if (inZone(t_x, t_y, 128, 16, 96, 136)) {    
      tft.fillRoundRect(128, 16, 96, 136, 10, PRESS_COLOR);
      delay(120);
      currentState = AC_SELECT; 
      drawACSelectionScreen();
    }
    else if (inZone(t_x, t_y, 16, 168, 96, 136)) {    
      tft.fillRoundRect(16, 168, 96, 136, 10, PRESS_COLOR);
      delay(120);
      currentState = LIGHT_SCREEN;
      drawLightScreen();
    }
    delay(50);
  }

  // --- AC SELECTION SCREEN ---
  else if (currentState == AC_SELECT) {
    if (inZone(t_x, t_y, 8, 8, 30, 30)) {              
      currentState = HOME;
      drawHomeScreen();
    }
    else if (inZone(t_x, t_y, 20, 60, 200, 60)) {      
      flashButtonPress(20, 60, 200, 60, "NAISHA'S AC");
      activeACProtocol = decode_type_t::HITACHI_AC; 
      activeACTitle = "NAISHA'S AC";
      currentState = AC_SCREEN;
      drawACScreen();
    }
    else if (inZone(t_x, t_y, 20, 140, 200, 60)) {     
      flashButtonPress(20, 140, 200, 60, "NEHA'S AC");
      activeACProtocol = decode_type_t::VOLTAS; 
      activeACTitle = "NEHA'S AC";
      currentState = AC_SCREEN;
      drawACScreen();
    }
    delay(150);
  }

  // --- AC REMOTE SCREEN ---
  else if (currentState == AC_SCREEN) {
    if (inZone(t_x, t_y, 8, 8, 30, 30)) {              
      currentState = AC_SELECT;
      drawACSelectionScreen();
    }
    else if (inZone(t_x, t_y, 70, 75, 100, 40)) {      
      acPower = !acPower;
      drawACScreen(); 
      sendACState();
    }
    else if (inCircle(t_x, t_y, 55, 135, 30)) {        
      flashCircle(55, 135, 30, "-");
      acTemp--;
      updateACTemp();
      sendACState();
    }
    else if (inCircle(t_x, t_y, 185, 135, 30)) {       
      flashCircle(185, 135, 30, "+");
      acTemp++;
      updateACTemp();
      sendACState();
    }
    else if (inZone(t_x, t_y, 202, 8, 30, 30)) {       
      fanSpeed = (fanSpeed % 3) + 1;
      tft.fillRoundRect(202, 8, 30, 30, 4, PRESS_COLOR);
      delay(100);
      drawFanSpeedIcon();
      sendACState();
    }
    else if (inZone(t_x, t_y, 16, 175, 96, 40)) {      
      flashButtonPress(16, 175, 96, 40, acModes[acModeIndex]);
      acModeIndex = (acModeIndex + 1) % 4;
      updateACMode();
      sendACState();
    }
    else if (inZone(t_x, t_y, 128, 175, 96, 40)) {     
      swingOn = !swingOn;
      drawToggleButton(128, 175, 96, 40, "SWING", swingOn);
      sendACState();
    }
    else if (inZone(t_x, t_y, 16, 225, 96, 40)) {      
      fanSpeed = (fanSpeed % 3) + 1;
      flashButtonPress(16, 225, 96, 40, "FAN: " + String(fanSpeed));
      drawFanSpeedIcon(); 
      sendACState();
    }
    else if (inZone(t_x, t_y, 128, 225, 96, 40)) {     
      turboOn = !turboOn;
      drawToggleButton(128, 225, 96, 40, "TURBO", turboOn);
      sendACState();
    }
    else if (inZone(t_x, t_y, 16, 275, 208, 40)) {     
      acLightOn = !acLightOn;
      drawToggleButton(16, 275, 208, 40, "LIGHT", acLightOn);
      sendACState();
    }
    delay(150);
  }

  // --- TV SCREEN (Samsung IR & FireTV BLE HID) ---
  else if (currentState == TV_SCREEN) {
    if (inZone(t_x, t_y, 8, 8, 30, 30)) {              
      currentState = HOME;
      drawHomeScreen();
    }
    else if (inZone(t_x, t_y, 162, 8, 70, 30)) {       
      tvPower = !tvPower;
      irsend.sendSAMSUNG(0xF40B0707, 32); 
      drawTVScreen(); 
    }
    else if (inZone(t_x, t_y, 8, 288, 108, 30)) {      
      flashButtonPress(8, 288, 108, 30, "VOL -");
      irsend.sendSAMSUNG(0xE0E0D02F, 32); 
    }
    else if (inZone(t_x, t_y, 124, 288, 108, 30)) {    
      flashButtonPress(124, 288, 108, 30, "VOL +");
      irsend.sendSAMSUNG(0xE0E0E01F, 32); 
    }
    else if (inZone(t_x, t_y, 88, 50, 64, 50)) {        
      flashButtonPress(88, 50, 64, 50, "^");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_UP_ARROW);
    }
    else if (inZone(t_x, t_y, 16, 106, 64, 50)) {       
      flashButtonPress(16, 106, 64, 50, "<");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_LEFT_ARROW);
    }
    else if (inZone(t_x, t_y, 88, 106, 64, 50)) {       
      flashButtonPress(88, 106, 64, 50, "OK");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_RETURN);
    }
    else if (inZone(t_x, t_y, 160, 106, 64, 50)) {      
      flashButtonPress(160, 106, 64, 50, ">");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_RIGHT_ARROW);
    }
    else if (inZone(t_x, t_y, 88, 162, 64, 50)) {       
      flashButtonPress(88, 162, 64, 50, "v");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_DOWN_ARROW);
    }
    else if (inZone(t_x, t_y, 8, 220, 70, 30)) {        
      flashButtonPress(8, 220, 70, 30, "BACK");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_ESC);
    }
    else if (inZone(t_x, t_y, 85, 220, 70, 30)) {       
      flashButtonPress(85, 220, 70, 30, "HOME");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_MEDIA_WWW_HOME);
    }
    else if (inZone(t_x, t_y, 8, 254, 70, 30)) {        
      flashButtonPress(8, 254, 70, 30, "<<");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_LEFT_ARROW); 
    }
    else if (inZone(t_x, t_y, 85, 254, 70, 30)) {       
      flashButtonPress(85, 254, 70, 30, "> ||");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
    }
    else if (inZone(t_x, t_y, 162, 254, 70, 30)) {      
      flashButtonPress(162, 254, 70, 30, ">>");
      if (bleKeyboard.isConnected()) bleKeyboard.write(KEY_RIGHT_ARROW); 
    }
    
    delay(150);
  }

  // --- LIGHT SCREEN ---
  else if (currentState == LIGHT_SCREEN) {
    if (inZone(t_x, t_y, 8, 8, 30, 30)) {              
      currentState = HOME;
      drawHomeScreen();
      delay(150);
    }
    else if (inZone(t_x, t_y, 160, 8, 72, 32)) {       
      lightOn = !lightOn;
      drawMasterToggle();
      sendSyskaState(); 
      delay(200);
    }
    else if (inZone(t_x, t_y, 16, 214, 208, 28)) {     
      int rel = constrain((int)t_x - 16, 0, 207);
      shadeValue = map(rel, 0, 207, 0, 255);
      drawShadeSlider();
      sendSyskaState(); 
    }
    else if (inZone(t_x, t_y, 16, 268, 208, 28)) {     
      int rel = constrain((int)t_x - 16, 0, 207);
      brightnessValue = map(rel, 0, 207, 0, 255);
      drawBrightnessSlider();
      sendSyskaState(); 
    }
    else {
      for (int idx = 0; idx < 16; idx++) {             
        int c = idx % 4, r = idx / 4;
        int x = 8 + c * 56, y = 50 + r * 36;
        if (inZone(t_x, t_y, x, y, 52, 32)) {
          selectedColorIndex = idx;
          drawColorGrid();
          drawShadeSlider(); 
          sendSyskaState(); 
          delay(150);
          break;
        }
      }
    }
  }
} 

// ============================================
// IR PROTOCOL SYNTHESIS
// ============================================
void sendACState() {
  ac.next.protocol = activeACProtocol; 
  ac.next.power = acPower;             
  ac.next.celsius = true;
  ac.next.degrees = acTemp; 
  
  if (acModes[acModeIndex] == "COOL") ac.next.mode = stdAc::opmode_t::kCool;
  else if (acModes[acModeIndex] == "DRY") ac.next.mode = stdAc::opmode_t::kDry;
  else if (acModes[acModeIndex] == "FAN") ac.next.mode = stdAc::opmode_t::kFan;
  else ac.next.mode = stdAc::opmode_t::kAuto;

  if (fanSpeed == 1) ac.next.fanspeed = stdAc::fanspeed_t::kLow;
  else if (fanSpeed == 2) ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
  else if (fanSpeed == 3) ac.next.fanspeed = stdAc::fanspeed_t::kHigh;

  ac.next.swingv = swingOn ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  ac.next.turbo = turboOn;
  ac.next.light = acLightOn;

  ac.sendAc(); 
}

// ============================================
// GEOMETRIC HIT-TESTING
// ============================================
bool inZone(int px, int py, int x, int y, int w, int h) {
  return (px > x && px < x + w && py > y && py < y + h);
}
bool inCircle(int px, int py, int cx, int cy, int r) {
  long dx = px - cx, dy = py - cy;
  return (dx * dx + dy * dy) <= (long)r * r;
}

// ============================================
// GUI RENDERING: HOME
// ============================================
void drawHomeScreen() {
  tft.fillScreen(BG_COLOR);

  tft.fillRoundRect(16, 16, 96, 136, 10, CARD_TV);
  tft.setTextColor(TEXT_DARK, CARD_TV);
  tft.drawCentreString("TV", 64, 75, 4);

  tft.fillRoundRect(128, 16, 96, 136, 10, CARD_AC);
  tft.setTextColor(TEXT_DARK, CARD_AC);
  tft.drawCentreString("AC", 176, 75, 4);

  tft.fillRoundRect(16, 168, 96, 136, 10, CARD_LIGHT);
  tft.setTextColor(TEXT_DARK, CARD_LIGHT); 
  tft.drawCentreString("LIGHT", 64, 227, 4);

  int cx = 176, cy = 227;
  tft.drawCircle(cx, cy, 32, REMOTE_BTN);
  tft.drawCircle(cx, cy, 31, REMOTE_BTN);
  tft.fillRect(cx - 16, cy - 2, 32, 4, TEXT_COLOR);
  tft.fillRect(cx - 2, cy - 16, 4, 32, TEXT_COLOR);
}

// ============================================
// GUI RENDERING: AC 
// ============================================
void drawACSelectionScreen() {
  tft.fillScreen(BG_COLOR);
  drawBackArrow();
  
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.drawCentreString("ROOM SELECT", 120, 20, 4);

  drawButton(20, 60, 200, 60, "NAISHA'S AC");
  drawButton(20, 140, 200, 60, "NEHA'S AC");
}

void drawACScreen() {
  tft.fillScreen(BG_COLOR);
  drawBackArrow();
  drawFanSpeedIcon();

  tft.setTextColor(DIM_GREY, BG_COLOR);
  tft.drawCentreString(activeACTitle, 120, 12, 2);

  uint16_t powerCol = acPower ? POWER_ON_GRN : POWER_OFF_RED;
  tft.fillRoundRect(70, 75, 100, 40, 6, powerCol);
  tft.setTextColor(acPower ? TEXT_DARK : TEXT_COLOR, powerCol);
  tft.drawCentreString(acPower ? "ON" : "OFF", 120, 87, 4);

  updateACTemp();

  tft.fillCircle(55, 135, 30, REMOTE_BTN);
  tft.setTextColor(TEXT_COLOR, REMOTE_BTN);
  tft.drawCentreString("-", 55, 120, 4);

  tft.fillCircle(185, 135, 30, REMOTE_BTN);
  tft.drawCentreString("+", 185, 120, 4);

  updateACMode();
  drawToggleButton(128, 175, 96, 40, "SWING", swingOn);
  drawButton(16, 225, 96, 40, "FAN: " + String(fanSpeed)); 
  drawToggleButton(128, 225, 96, 40, "TURBO", turboOn);
  drawToggleButton(16, 275, 208, 40, "LIGHT", acLightOn);
}

void updateACTemp() {
  tft.fillRect(60, 40, 120, 30, BG_COLOR);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.drawCentreString(String(acTemp) + " C", 120, 45, 4);
}

void updateACMode() {
  drawButton(16, 175, 96, 40, acModes[acModeIndex]);
}

void drawFanSpeedIcon() {
  int x = 202, y = 8, w = 30, h = 30;
  tft.fillRoundRect(x, y, w, h, 4, REMOTE_BTN);
  int barW = 5, gap = 3;
  int baseY = y + h - 4;
  for (int i = 0; i < fanSpeed; i++) {
    int barH = 6 + i * 6;
    int bx = x + 4 + i * (barW + gap);
    tft.fillRect(bx, baseY - barH, barW, barH, TEXT_COLOR);
  }
}

// ============================================
// GUI RENDERING: TV 
// ============================================
void drawTVScreen() {
  tft.fillScreen(BG_COLOR);
  drawBackArrow();

  uint16_t tvPwrCol = tvPower ? POWER_ON_GRN : POWER_OFF_RED;
  tft.fillRoundRect(162, 8, 70, 30, 4, tvPwrCol);
  tft.setTextColor(tvPower ? TEXT_DARK : TEXT_COLOR, tvPwrCol);
  tft.drawCentreString("PWR", 197, 15, 2);

  drawButton(88, 50, 64, 50, "^");
  drawButton(16, 106, 64, 50, "<");
  drawButton(88, 106, 64, 50, "OK");
  drawButton(160, 106, 64, 50, ">");
  drawButton(88, 162, 64, 50, "v");

  drawButton(8, 220, 70, 30, "BACK");
  drawButton(85, 220, 70, 30, "HOME");
  drawButton(162, 220, 70, 30, "MENU");

  drawButton(8, 254, 70, 30, "<<");
  drawButton(85, 254, 70, 30, "> ||");
  drawButton(162, 254, 70, 30, ">>");

  drawButton(8, 288, 108, 30, "VOL -");
  drawButton(124, 288, 108, 30, "VOL +");
}

// ============================================
// SHARED UI HELPERS
// ============================================
void drawBackArrow() {
  tft.fillRoundRect(8, 8, 30, 30, 4, REMOTE_BTN);
  tft.setTextColor(TEXT_COLOR, REMOTE_BTN);
  tft.drawCentreString("<", 23, 14, 2);
}

void drawButton(int x, int y, int w, int h, String label) {
  tft.fillRoundRect(x, y, w, h, 6, REMOTE_BTN);
  tft.setTextColor(TEXT_COLOR, REMOTE_BTN);
  tft.drawCentreString(label, x + w / 2, y + h / 2 - 8, 2);
}

void flashButtonPress(int x, int y, int w, int h, String label) {
  tft.fillRoundRect(x, y, w, h, 6, PRESS_COLOR);
  tft.setTextColor(BG_COLOR, PRESS_COLOR);
  tft.drawCentreString(label, x + w / 2, y + h / 2 - 8, 2);
  delay(120); 
  drawButton(x, y, w, h, label);
}

void flashCircle(int cx, int cy, int r, String label) {
  tft.fillCircle(cx, cy, r, PRESS_COLOR);
  tft.setTextColor(BG_COLOR, PRESS_COLOR);
  tft.drawCentreString(label, cx, cy - 15, 4);
  delay(120);
  tft.fillCircle(cx, cy, r, REMOTE_BTN);
  tft.setTextColor(TEXT_COLOR, REMOTE_BTN);
  tft.drawCentreString(label, cx, cy - 15, 4);
}

void drawToggleButton(int x, int y, int w, int h, String label, bool state) {
  uint16_t col = state ? PRESS_COLOR : REMOTE_BTN;
  tft.fillRoundRect(x, y, w, h, 6, col);
  tft.setTextColor(TEXT_COLOR, col);
  tft.drawCentreString(label, x + w / 2, y + h / 2 - 8, 2);
}

// ============================================
// GUI RENDERING: LIGHTING
// ============================================
void drawLightScreen() {
  tft.fillScreen(BG_COLOR);
  drawBackArrow();
  drawMasterToggle();
  drawColorGrid();
  drawShadeSlider();
  drawBrightnessSlider();
}

void drawMasterToggle() {
  uint16_t col = lightOn ? PRESS_COLOR : REMOTE_BTN;
  tft.fillRoundRect(160, 8, 72, 32, 6, col);
  tft.setTextColor(TEXT_COLOR, col);
  tft.drawCentreString(lightOn ? "ON" : "OFF", 196, 16, 2);
}

void drawColorGrid() {
  int idx = 0;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int x = 8 + c * 56;
      int y = 50 + r * 36;
      tft.fillRoundRect(x, y, 52, 32, 6, presetColors[idx]);
      if (idx == selectedColorIndex) {
        tft.drawRoundRect(x - 2, y - 2, 56, 36, 8, TEXT_COLOR);
      } else {
        tft.drawRoundRect(x - 2, y - 2, 56, 36, 8, BG_COLOR); 
      }
      idx++;
    }
  }
}

void drawShadeSlider() {
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.drawString("SHADE", 16, 196, 2);
  int x0 = 16, y0 = 214, w = 208, h = 28;
  tft.fillRect(x0 - 4, y0 - 4, w + 8, h + 8, BG_COLOR); 
  uint16_t hue = presetColors[selectedColorIndex];
  uint8_t hr, hg, hb;
  color565toRGB(hue, hr, hg, hb);
  for (int i = 0; i < w; i++) {
    float t = (float)i / (w - 1);
    uint8_t r, g, b;
    if (t < 0.5) {
      float k = t / 0.5;
      r = lerp8(255, hr, k);
      g = lerp8(255, hg, k);
      b = lerp8(255, hb, k);
    } else {
      float k = (t - 0.5) / 0.5;
      r = lerp8(hr, 0, k);
      g = lerp8(hg, 0, k);
      b = lerp8(hb, 0, k);
    }
    tft.drawFastVLine(x0 + i, y0, h, tft.color565(r, g, b));
  }
  int knobX = x0 + (int)((float)shadeValue / 255.0 * (w - 1));
  tft.fillRect(knobX - 2, y0 - 4, 4, h + 8, TEXT_COLOR);
}

void drawBrightnessSlider() {
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.drawString("BRIGHTNESS", 16, 250, 2);
  int x0 = 16, y0 = 268, w = 208, h = 28;
  tft.fillRect(x0 - 4, y0 - 4, w + 8, h + 8, BG_COLOR);
  for (int i = 0; i < w; i++) {
    float t = (float)i / (w - 1);
    uint8_t v = lerp8(0, 255, t);
    tft.drawFastVLine(x0 + i, y0, h, tft.color565(v, v, v));
  }
  int knobX = x0 + (int)((float)brightnessValue / 255.0 * (w - 1));
  tft.fillRect(knobX - 2, y0 - 4, 4, h + 8, TEXT_COLOR);
}

uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  return (uint8_t)(a + (b - a) * t);
}

void color565toRGB(uint16_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = ((color >> 11) & 0x1F) * 255 / 31;
  g = ((color >> 5) & 0x3F) * 255 / 63;
  b = (color & 0x1F) * 255 / 31;
}

// ============================================
// SCREENSAVER (NTP Time & Eye Animation)
// ============================================
void drawScreensaverBase() {
  tft.fillScreen(BG_COLOR);
  eyeOffsetX = 0;
  eyeHeight = 50;
  drawEyes();
  lastEyeChange = millis();
  lastClockUpdate = 0; 
}

void drawEyes() {
  tft.fillRect(0, 140, 240, 120, BG_COLOR);
  int leftX  = 65;
  int rightX = 175;
  int eyeY = 190;
  int eyeWidth = 56;
  uint16_t dragonEyeColor = 0x07E0; 
  
  tft.fillRoundRect(leftX  - eyeWidth / 2, eyeY - eyeHeight / 2, eyeWidth, eyeHeight, 16, dragonEyeColor);
  tft.fillRoundRect(rightX - eyeWidth / 2, eyeY - eyeHeight / 2, eyeWidth, eyeHeight, 16, dragonEyeColor);

  if (eyeHeight > 12) {
    int pupilRadius = 14; 
    
    tft.fillCircle(leftX + eyeOffsetX, eyeY, pupilRadius, BG_COLOR);
    tft.fillCircle(rightX + eyeOffsetX, eyeY, pupilRadius, BG_COLOR);
    
    tft.fillCircle((leftX + eyeOffsetX) + 4, eyeY - 5, 2, TFT_WHITE);
    tft.fillCircle((rightX + eyeOffsetX) + 4, eyeY - 5, 2, TFT_WHITE);
  }
}

void updateScreensaverAnimation() {
  // Sync global time from NTP server
  if (millis() - lastClockUpdate > 1000 || lastClockUpdate == 0) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeStr[6];
      strftime(timeStr, 6, "%H:%M", &timeinfo);
      
      tft.fillRect(80, 15, 80, 30, BG_COLOR); 
      tft.setTextColor(DIM_GREY, BG_COLOR);
      tft.drawCentreString(timeStr, 120, 20, 4); 
    } else {
      tft.fillRect(80, 15, 80, 30, BG_COLOR); 
      tft.setTextColor(DIM_GREY, BG_COLOR);
      tft.drawCentreString("--:--", 120, 20, 4);
    }
    lastClockUpdate = millis();
  }

  // Eye movement randomization
  static unsigned long nextChangeDelay = 3000;
  if (millis() - lastEyeChange > nextChangeDelay) {
    int action = random(0, 4);
    if (action == 0) {
      eyeOffsetX = random(-20, 21); 
      eyeHeight = 50;
    } else if (action == 1) { 
      int normalHeight = eyeHeight;
      eyeHeight = 6;
      drawEyes();
      delay(120);
      eyeHeight = normalHeight;
    } else if (action == 2) { 
      eyeHeight = 60;
      eyeOffsetX = 0;
    } else { 
      eyeHeight = 50;
      eyeOffsetX = 0;
    }
    drawEyes();
    lastEyeChange = millis();
    nextChangeDelay = random(2000, 5000);
  }
}