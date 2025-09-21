
#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 15

#define DISPLAYTIMEOUT 1500

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h> // Use this instead of ST7735_t3

#include <Fonts/Org_01.h>
#include "Yeysk16pt7b.h"
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansOblique24pt7b.h>
#include <Fonts/FreeSansBoldOblique24pt7b.h>

#define PULSE 1
#define VAR_TRI 2
#define FILTER_ENV 3
#define AMP_ENV1 4
#define AMP_ENV2 5

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

String presets[80] = { "11", "12", "13", "14", "15", "16", "17", "18", "21", "22", "23", "24", "25", "26", "27", "28", "31", "32", "33", "34", "35", "36", "37", "38", "41", "42", "43", "44", "45", "46", "47", "48", "51", "52", "53", "54", "55", "56", "57", "58", "61", "62", "63", "64", "65", "66", "67", "68", "71", "72", "73", "74", "75", "76", "77", "78", "81", "82", "83", "84", "85", "86", "87", "88", "91", "92", "93", "94", "95", "96", "97", "98", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8" };

String currentParameter = "";
String currentValue = "";
float currentFloatValue = 0.0;
String currentPgmNum = "";
String currentPatchName = "";
String newPatchName = "";
const char *currentSettingsOption = "";
const char *currentSettingsValue = "";
int currentSettingsPart = SETTINGS;
int paramType = PARAMETER;

unsigned long timer = 0;

void startTimer() {
  if (state == PARAMETER) {
    timer = millis();
  }
}

void renderBootUpPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.drawRect(42, 30, 46, 11, ST7735_WHITE);
  tft.fillRect(88, 30, 61, 11, ST7735_WHITE);
  tft.setCursor(45, 31);
  tft.setFont(&Org_01);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.println("KORG");
  tft.setTextColor(ST7735_BLACK);
  tft.setCursor(91, 37);
  tft.println("EDITOR");
  tft.setTextColor(ST7735_YELLOW);
  tft.setFont(&Yeysk16pt7b);
  tft.setCursor(0, 70);
  tft.setTextSize(1);
  tft.println("Poly-61");
  tft.setTextColor(ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(110, 95);
  tft.println(VERSION);
}

void renderCurrentPatchPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 33);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println(currentPgmNum);
  int Patchnumber = currentPgmNum.toInt();
  if (Patchnumber <= 32) {
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(65, 33);
    tft.println("P:");
    tft.setCursor(90, 33);
    tft.setTextColor(ST7735_RED);
    tft.setTextSize(1);
    tft.println(presets[Patchnumber - 1]);
  }
  if (Patchnumber > 80 && Patchnumber <= 160) {
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(65, 33);
    tft.println("Bank 1:");
  }
  if (Patchnumber > 160 && Patchnumber <= 240) {
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(65, 33);
    tft.println("Bank 2:");
  }
  if (Patchnumber > 240 && Patchnumber <= 320) {
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(65, 33);
    tft.println("Bank 3:");
  }
  if (Patchnumber > 320 && Patchnumber <= 400) {
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(65, 33);
    tft.println("Bank 4:");
  }
  tft.setTextColor(ST7735_BLACK);
  tft.setFont(&Org_01);

  tft.drawFastHLine(10, 42, tft.width() - 20, ST7735_RED);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(1, 70);
  tft.setTextColor(ST7735_WHITE);
  tft.println(currentPatchName);
}

void renderPulseWidth(float value) {
  tft.drawFastHLine(108, 74, 15 + (value * 13), ST7735_CYAN);
  tft.drawFastVLine(123 + (value * 13), 74, 20, ST7735_CYAN);
  tft.drawFastHLine(123 + (value * 13), 94, 16 - (value * 13), ST7735_CYAN);
  if (value < 0) {
    tft.drawFastVLine(108, 74, 21, ST7735_CYAN);
  } else {
    tft.drawFastVLine(138, 74, 21, ST7735_CYAN);
  }
}

void renderVarTriangle(float value) {
  tft.drawLine(110, 94, 123 + (value * 13), 74, ST7735_CYAN);
  tft.drawLine(123 + (value * 13), 74, 136, 94, ST7735_CYAN);
}

void renderEnv(float att, float dec, float sus, float rel) {
  tft.drawLine(100, 94, 100 + (att * 60), 74, ST7735_CYAN);
  tft.drawLine(100 + (att * 60), 74.0, 100 + ((att + dec) * 60), 94 - (sus / 52), ST7735_CYAN);
  tft.drawFastHLine(100 + ((att + dec) * 60), 94 - (sus / 52), 40 - ((att + dec) * 60), ST7735_CYAN);
  tft.drawLine(139, 94 - (sus / 52), 139 + (rel * 60), 94, ST7735_CYAN);
}

void renderCurrentParameterPage() {
  switch (state) {
    case PARAMETER:
      tft.fillScreen(ST7735_BLACK);
      tft.setFont(&FreeSans12pt7b);
      tft.setCursor(0, 33);
      tft.setTextColor(ST7735_YELLOW);
      tft.setTextSize(1);
      tft.println(currentParameter);
      tft.drawFastHLine(10, 42, tft.width() - 20, ST7735_RED);
      tft.setCursor(1, 70);
      tft.setTextColor(ST7735_WHITE);
      tft.println(currentValue);
      switch (paramType) {
        case PULSE:
          renderPulseWidth(currentFloatValue);
          break;
        case VAR_TRI:
          renderVarTriangle(currentFloatValue);
          break;
        case FILTER_ENV:
          renderEnv(eg1_attack * 0.0001, eg1_decay * 0.0001, eg1_sustain, eg1_release * 0.0001);
          break;
      }
      break;
  }
}

void renderDeletePatchPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 33);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Delete?");
  tft.drawFastHLine(10, 40, tft.width() - 20, ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 58);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 58);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);
  tft.fillRect(0, 65, tft.width(), 23, ST77XX_RED);
  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.first().patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.first().patchName);
}

void renderDeleteMessagePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(2, 33);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Renumbering");
  tft.setCursor(10, 70);
  tft.println("SD Card");
}

void renderSysexMessagePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(2, 33);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Sysex Dump");
  tft.setCursor(10, 70);
  tft.println("Received");
}

void renderSavePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 33);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Save?");
  tft.drawFastHLine(10, 40, tft.width() - 20, ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 58);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches[patches.size() - 2].patchNo);
  tft.setCursor(35, 58);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches[patches.size() - 2].patchName);
  tft.fillRect(0, 65, tft.width(), 23, ST77XX_RED);
  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);
}

void renderReinitialisePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(5, 33);
  tft.println("Initialise to");
  tft.setCursor(5, 70);
  tft.println("panel setting");
}

void renderPatchNamingPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(0, 33);
  tft.println("Rename Patch");
  tft.drawFastHLine(10, 62, tft.width() - 20, ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 70);
  tft.println(newPatchName);
}

void renderRecallPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 25);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 25);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);

  tft.fillRect(0, 36, tft.width(), 23, 0xA000);
  tft.setCursor(0, 52);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.first().patchNo);
  tft.setCursor(35, 52);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.first().patchName);

  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  patches.size() > 1 ? tft.println(patches[1].patchNo) : tft.println(patches.last().patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  patches.size() > 1 ? tft.println(patches[1].patchName) : tft.println(patches.last().patchName);
}

void showRenamingPage(String newName) {
  newPatchName = newName;
}

void renderUpDown(uint16_t x, uint16_t y, uint16_t colour) {
  //Produces up/down indicator glyph at x,y
  tft.setCursor(x, y);
  tft.fillTriangle(x, y, x + 8, y - 8, x + 16, y, colour);
  tft.fillTriangle(x, y + 4, x + 8, y + 12, x + 16, y + 4, colour);
}


void renderSettingsPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(0, 33);
  tft.println(currentSettingsOption);
  if (currentSettingsPart == SETTINGS) renderUpDown(140, 22, ST7735_YELLOW);
  tft.drawFastHLine(10, 42, tft.width() - 20, ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 70);
  tft.println(currentSettingsValue);
  if (currentSettingsPart == SETTINGSVALUE) renderUpDown(140, 60, ST7735_WHITE);
}

void showCurrentParameterPage(const char *param, float val, int pType) {
  currentParameter = param;
  currentValue = String(val);
  currentFloatValue = val;
  paramType = pType;
  startTimer();
}

void showCurrentParameterPage(const char *param, String val, int pType) {
  if (state == SETTINGS || state == SETTINGSVALUE) state = PARAMETER;  //Exit settings page if showing
  currentParameter = param;
  currentValue = val;
  paramType = pType;
  startTimer();
}

void showCurrentParameterPage(const char *param, String val) {
  showCurrentParameterPage(param, val, PARAMETER);
}

void showPatchPage(String number, String patchName) {
  currentPgmNum = number;
  currentPatchName = patchName;
}

void showSettingsPage(const char *option, const char *value, int settingsPart) {
  currentSettingsOption = option;
  currentSettingsValue = value;
  currentSettingsPart = settingsPart;
}

void refreshScreen() {

    switch (state) {
      case PARAMETER:
        if ((millis() - timer) > DISPLAYTIMEOUT) {
          renderCurrentPatchPage();
        } else {
          renderCurrentParameterPage();
        }
        break;
      case RECALL:
        renderRecallPage();
        break;
      case SAVE:
        renderSavePage();
        break;
      case REINITIALISE:
        renderReinitialisePage();
        delay(500);
        state = PARAMETER;
        break;
      case PATCHNAMING:
        renderPatchNamingPage();
        break;
      case PATCH:
        renderCurrentPatchPage();
        break;
      case DELETE:
        renderDeletePatchPage();
        break;
      case DELETEMSG:
        renderDeleteMessagePage();
        break;
      case SETTINGS:
      case SETTINGSVALUE:
        renderSettingsPage();
        break;
    }
}

void setupDisplay() {
  tft.initR(INITR_MINI160x80_PLUGIN); // 160x80 IPS
  tft.setRotation(3);          // Rotate if needed
  //tft.invertDisplay(true);
  tft.fillScreen(ST77XX_BLACK);
  renderBootUpPage();
}
