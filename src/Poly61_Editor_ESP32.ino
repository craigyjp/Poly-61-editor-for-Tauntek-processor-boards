/*
  Poly61 Editor Encoders -
  For Tauntek Equipped Poly 61 synts

  Includes code by:
    ElectroTechnique for general method of menus and updates.

  Additional libraries:
    Agileware CircularBuffer available in Arduino libraries manager
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <MIDI.h>
#include <HardwareSerial.h>
#include "MidiCC.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "Button.h"
#include "HWControls.h"
#include "EepromMgr.h"


#define PARAMETER 0      //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1         //Patches list
#define SAVE 2           //Save patch page
#define REINITIALISE 3   // Reinitialise message
#define PATCH 4          // Show current patch bypassing PARAMETER
#define PATCHNAMING 5    // Patch naming page
#define DELETE 6         //Delete patch page
#define DELETEMSG 7      //Delete patch message page
#define SETTINGS 8       //Settings page
#define SETTINGSVALUE 9  //Settings page

unsigned int state = PARAMETER;

#include "ST7735Display.h"

boolean cardStatus = false;


//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI);
//MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI5);

#include "Settings.h"

int patchNo = 1;  //Current patch no

void pollAllMCPs();

void initRotaryEncoders();

void initButtons();

int getEncoderSpeed(int id);

void setup() {

  Serial.begin(115200);

  SPI.begin(18, 19, 23);
  setupDisplay();
  Wire.begin();
  Wire.setClock(400000);  // Slow down I2C to 100kHz

  mcp1.begin(0);
  delay(10);
  mcp2.begin(1);
  delay(10);
  mcp3.begin(2);
  delay(10);
  mcp4.begin(3);
  delay(10);

  //groupEncoders();
  initRotaryEncoders();
  initButtons();

  EEPROM.begin(512);  // or whatever size you need

  mcp1.pinMode(7, OUTPUT);   // pin 7 = GPA7 of MCP2301X
  mcp1.pinMode(15, OUTPUT);  // pin 15 = GPB7 of MCP2301X

  mcp2.pinMode(7, OUTPUT);   // pin 7 = GPA7 of MCP2301X
  mcp2.pinMode(14, OUTPUT);  // pin 14 = GPB6 of MCP2301X
  mcp2.pinMode(15, OUTPUT);  // pin 15 = GPB7 of MCP2301X

  mcp3.pinMode(15, OUTPUT);  // pin 15 = GPB7 of MCP2301X

  mcp4.pinMode(7, OUTPUT);   // pin 7 = GPA7 of MCP2301X
  mcp4.pinMode(13, OUTPUT);  // pin 15 = GPB7 of MCP2301X
  mcp4.pinMode(14, OUTPUT);  // pin 14 = GPB6 of MCP2301X
  mcp4.pinMode(15, OUTPUT);  // pin 15 = GPB7 of MCP2301X


  for (int i = 0; i < NUM_ENCODERS; i++) {
    lastTransition[i] = millis();  // ✅ no warning now
  }

  setUpSettings();
  setupHardware();

  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachHalfQuad(ENCODER_PINA, ENCODER_PINB);  // Your encoder pins
  encoder.setCount(0);
  encPrevious = encoder.getCount();

  // --- Initialize SD ---

  if (!SD.begin(13)) {  // CS pin
    Serial.println("SD card mount failed!");
    while (1)
      ;
  }

  Serial.println("SD card mounted.");
  loadPatches();  // Must be called before encoder logic
  if (patches.isEmpty()) {
    //Serial.println("⚠️ No patches found after loadPatches()");
  } else {
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);  // Optional: auto-load first patch
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();

  Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //Read UpdateParams type from EEPROM
  updateParams = getUpdateParams();

  //MIDI 5 Pin DIN
  Serial2.begin(31250, SERIAL_8N1, 16, 17);  // RX, TX
  MIDI.begin();
  MIDI.setHandleControlChange(myConvertControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  MIDI.setHandleSystemExclusive(handleSysexByte);
  MIDI.turnThruOn(midi::Thru::Mode::Off);
  Serial.println("MIDI In DIN Listening");

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();

  //Read MIDI Out Channel from EEPROM
  midiOutCh = getMIDIOutCh();

  //Read Bank from EEPROM
  bankselect = getSetBank();

  // Read the encoders accelerate
  accelerate = getEncoderAccelerate();

  // read in aftertouch setting
  afterTouch = getAfterTouch();

  recallPatch(patchNo);  //Load first patch
  refreshScreen();
}

void initRotaryEncoders() {
  for (auto &rotaryEncoder : rotaryEncoders) {
    rotaryEncoder.init();
  }
}

void initButtons() {
  for (auto &button : allButtons) {
    button->begin();
  }
}

void myNoteOn(byte channel, byte note, byte velocity) {
  if (!recallPatchFlag) {
    MIDI.sendNoteOn(note, velocity, channel);
  }
}

void myNoteOff(byte channel, byte note, byte velocity) {
  if (!recallPatchFlag) {
    MIDI.sendNoteOff(note, velocity, channel);
  }
}

void handleSysexByte(byte *data, unsigned length) {
  if (length < 6) return;  // safety: need at least header + F7

  dumpType = data[4];  // 5th byte in header determines type

  switch (dumpType) {
    case 0x31:  // ---- Poly61 style, 1926 bytes, no names ----
      {
        // Reset state
        receivingSysEx = true;
        byteIndex = 0;
        currentBlock = 0;
        headerSkip = 0;

        // Skip the 5-byte header and tail F7
        unsigned payloadLen = length - 6;

        for (unsigned i = 0; i < payloadLen; i++) {
          ramArray[currentBlock][byteIndex] = data[i + 5];
          byteIndex++;

          if (byteIndex >= 24) {  // 24 nibbles = 12 bytes
            byteIndex = 0;
            currentBlock++;
          }
        }

        if (currentBlock >= 80) {
          sysexComplete = true;
          receivingSysEx = false;
          Serial.println("Processed sysex (0x01 bank without names)");
        }
      }
      break;

    case 0x02:  // ---- Single patch with name ----
      processSinglePatch(&data[5], length - 6);
      recallPatchFlag = true;
      sendToSynthData();
      updatePatchname();
      startParameterDisplay();
      recallPatchFlag = false;
      break;

    case 0x03:  // ---- Bank of 80 patches with names ----
      processBankPatch(&data[5], length - 6);
      break;

    default:
      Serial.print("Unknown SysEx dump type: 0x");
      Serial.println(dumpType, HEX);
      break;
  }
}

void processSinglePatch(byte *payload, unsigned len) {
  if (len < 50) return;  // need 50 nibbles (26 name + 24 patch)

  // First 26 nibbles = patch name (13 bytes)
  char name[14];
  for (int i = 0; i < 13; i++) {
    byte hi = payload[i * 2];
    byte lo = payload[i * 2 + 1];
    name[i] = ((hi & 0x0F) << 4) | (lo & 0x0F);
  }
  name[13] = '\0';
  patchName = String(name);

  // Next 24 nibbles = patch data (12 bytes)
  byte patchBytes[12];
  int offset = 26;
  for (int i = 0; i < 12; i++) {
    byte hi = payload[(offset + i * 2)];
    byte lo = payload[(offset + i * 2) + 1];
    patchBytes[i] = ((hi & 0x0F) << 4) | (lo & 0x0F);
  }

  // Decode into current patch memory (don’t save yet)
  decodePatch(patchBytes);
  Serial.print("Loaded single patch: ");
  Serial.println(patchName);
}

void processBankPatch(byte *payload, unsigned len) {
  if (len < 50) return;  // 26 nibbles name + 24 nibbles data

  // ---- Extract patch name ----
  char name[14];
  for (int i = 0; i < 13; i++) {
    byte hi = payload[i * 2];
    byte lo = payload[i * 2 + 1];
    name[i] = ((hi & 0x0F) << 4) | (lo & 0x0F);
  }
  name[13] = '\0';
  patchName = String(name);

  // ---- Extract patch params ----
  byte patchBytes[12];
  int offset = 26;
  for (int i = 0; i < 12; i++) {
    byte hi = payload[offset + i * 2];
    byte lo = payload[offset + i * 2 + 1];
    patchBytes[i] = ((hi & 0x0F) << 4) | (lo & 0x0F);
  }

  // Decode + save to correct slot in selected bank
  int bankStart = 1;
  switch (bankselect) {
    case 0: bankStart = 1; break;
    case 1: bankStart = 81; break;
    case 2: bankStart = 161; break;
    case 3: bankStart = 241; break;
    case 4: bankStart = 301; break;
  }

  decodePatch(patchBytes);
  sprintf(buffer, "%d", bankStart + bankPatchCounter);
  savePatch(buffer, getCurrentPatchData());
  updatePatchname();

  bankPatchCounter++;

  // If we've now got all 80, finish the bank
  if (bankPatchCounter >= 80) {
    loadPatches();
    bankPatchCounter = 0;
    showCurrentParameterPage("Finished", String("Sysex Load"));
    startParameterDisplay();
    delay(50);
    recallPatch(bankStart);
    startParameterDisplay();
  }
}

void convertNibblesToBytes() {
  for (int p = 0; p < NUM_PATCHES; p++) {
    for (int b = 0; b < PATCH_BYTES; b++) {
      byte hi = ramArray[p][b * 2];      // high nibble
      byte lo = ramArray[p][b * 2 + 1];  // low nibble
      receivedPatches[p][b] = ((hi & 0x0F) << 4) | (lo & 0x0F);
    }
  }
  Serial.println("Converted nibbles to 12-byte patches");
}

// ------------------- Single Patch Decode -------------------
void decodePatch(byte *src) {
  // ---- Envelope 1 ----
  eg1_attack = (src[0] & 0x7F);   // ATT0–ATT6
  eg1_decay = (src[1] & 0x7F);    // DEC0–DEC6
  eg1_sustain = (src[2] & 0x7F);  // SUS0–SUS6
  eg1_release = (src[3] & 0x7F);  // REL0–REL6

  // ---- VCF ----
  vcf_cutoff = (src[4] & 0x7F);           // FC0–FC6
  vcf_key_follow = (src[4] >> 7) & 0x01;  // KBD
  vcf_res = (src[5] & 0x7F);              // RES0–RES6
  lfo_src = (src[5] >> 7) & 0x01;         // VIBLFO

  // ---- Oscillator 1 ----
  osc1_wave = ((src[0] >> 7) & 0x01) |  // W10
              ((src[1] >> 6) & 0x02);   // W11
  osc1_pwm = src[6] & 0x3F;             // PW0–PW5
  osc1_octave = (src[10] >> 3) & 0x03;  // OCT10–OCT11

  // ---- Oscillator 2 ----
  osc2_wave = ((src[2] >> 7) & 0x01) |  // W20
              ((src[3] >> 6) & 0x02);   // W21
  osc2_interval = src[8] & 0x0F;        // INT0–INT3
  osc2_octave = (src[9] >> 6) & 0x03;   // OCT20–OCT21
  osc2_detune = (src[9] >> 3) & 0x07;   // DT0–DT2

  // ---- LFO1 ----
  lfo1_vcf = src[7] & 0x0F;            // MGFI0–MGFI3
  lfo1_vco = (src[7] >> 4) & 0x0F;     // MGO0–MGO3
  lfo1_delay = (src[10] >> 5) & 0x07;  // MGD0–MGD2
  lfo1_wave = src[10] & 0x07;          // LFOW0–LFOW2
  lfo1_speed = src[11] & 0x3F;         // MGF0–MGF5

  // ---- LFO2 ----
  lfo2_wave = (src[6] >> 6) & 0x03;      // LFO2W0–LFO2W1
  lfo2_speed = ((src[8] >> 4) & 0x0F) |  // LFO2F0–F3
               ((src[11] >> 3) & 0x10);  // LFO2F4

  // ---- VCA ----
  vca_gate = (src[11] >> 7) & 0x01;  // EGM
  vcf_eg_depth = src[9] & 0x07;      // EGI0–EGI2
}

void decodePatches() {

  int bankStart = 1;
  switch (bankselect) {
    case 0: bankStart = 1; break;
    case 1: bankStart = 81; break;
    case 2: bankStart = 161; break;
    case 3: bankStart = 241; break;
    case 4: bankStart = 301; break;
  }
  for (int p = 0; p < NUM_PATCHES; p++) {
    // Decode one patch into globals
    decodePatch(receivedPatches[p]);

    // Replace name completely

    patchName = "Sysex " + String(bankStart + p);

    sprintf(buffer, "%d", p + bankStart);
    savePatch(buffer, getCurrentPatchData());
    updatePatchname();
  }

  loadPatches();

  // Recall first patch in the current bank
  switch (bankselect) {
    case 0: recallPatch(1); break;
    case 1: recallPatch(81); break;
    case 2: recallPatch(161); break;
    case 3: recallPatch(241); break;
    case 4: recallPatch(301); break;
  }

  state = PARAMETER;
  startParameterDisplay();
}

// Packs current patch parameters into 12-byte array for SysEx dump
void encodePatch(int patchIndex, byte *dst) {
  // ---- Envelope 1 ----
  dst[0] = (osc1_wave & 0x01) << 7 | (eg1_attack & 0x7F);   // W10 + ATT0–6
  dst[1] = (osc1_wave & 0x02) << 6 | (eg1_decay & 0x7F);    // W11 + DEC0–6
  dst[2] = (osc2_wave & 0x01) << 7 | (eg1_sustain & 0x7F);  // W20 + SUS0–6
  dst[3] = (osc2_wave & 0x02) << 6 | (eg1_release & 0x7F);  // W21 + REL0–6

  // ---- VCF ----
  dst[4] = ((vcf_key_follow & 0x01) << 7) | (vcf_cutoff & 0x7F);  // KBD + FC0–6
  dst[5] = ((lfo_src & 0x01) << 7) | (vcf_res & 0x7F);            // VIBLFO + RES0–6

  // ---- Oscillator 1 ----
  dst[6] = ((lfo2_wave & 0x03) << 6) | (osc1_pwm & 0x3F);  // LFO2W0–1 + PW0–5

  // ---- LFO1 ----
  dst[7] = ((lfo1_vco & 0x0F) << 4) | (lfo1_vcf & 0x0F);  // MGO0–3 + MGFI0–3

  // ---- LFO2 Speed + OSC2 Interval ----
  dst[8] = ((lfo2_speed & 0x0F) << 4) | (osc2_interval & 0x0F);  // LFO2F0–3 + INT0–3

  // ---- Oscillator 2 ----
  dst[9] = ((osc2_octave & 0x03) << 6) | ((osc2_detune & 0x07) << 3) | (vcf_eg_depth & 0x07);  // OCT20–21 + DT0–2 + EGI0–2

  // ---- LFO1 Delay + Wave + Osc1 Octave ----
  dst[10] = ((lfo1_delay & 0x07) << 5) | ((osc1_octave & 0x03) << 3) | (lfo1_wave & 0x07);  // MGD0–2 + OCT10–11 + LFOW0–2

  // ---- VCA + LFO1 Speed + LFO2 Speed (bit 4) ----
  dst[11] = ((vca_gate & 0x01) << 7) | ((lfo2_speed & 0x10) << 3) |  // LFO2F4
            (lfo1_speed & 0x3F);                                     // MGF0–5
}

void sendSysexDump() {
  if (saveAll) {
    sendingSysEx = true;
    showCurrentParameterPage("Processing", String("Sysex Send"));
    startParameterDisplay();

    const byte header[5] = { 0xF0, 0x42, 0x50, 0x36, 0x31 };
    const byte tail = 0xF7;

    // Total = 5 header + (80*24 nibbles) + 1 tail = 1926
    static byte sysexBuffer[1926];
    int idx = 0;

    // Copy header
    for (int i = 0; i < 5; i++) {
      sysexBuffer[idx++] = header[i];
    }

    // Encode 80 patches
    for (int p = 0; p < 80; p++) {
      updateParams = false;
      recallPatch(p + 1);

      byte packed[12];
      encodePatch(p, packed);

      // Split into 24 nibbles
      for (int i = 0; i < 12; i++) {
        sysexBuffer[idx++] = (packed[i] >> 4) & 0x0F;  // high nibble
        sysexBuffer[idx++] = packed[i] & 0x0F;         // low nibble
      }
    }

    // Add SysEx end
    sysexBuffer[idx++] = tail;

    // ✅ Send in one go using the MIDI library
    MIDI.sendSysEx(idx, sysexBuffer, true);

    // or raw UART if you like:
    // Serial2.write(sysexBuffer, idx);

    saveAll = false;
    storeSaveAll(saveAll);
    settings::decrement_setting_value();
    settings::save_current_value();
    showSettingsPage();
    delay(100);
    sendingSysEx = false;
    state = PARAMETER;
    recallPatch(1);
    recallPatchFlag = false;
    updateParams = true;
  }
}

void sendPatchWithHeader(const String &patchName, int patchIndex, byte headerType) {
  byte buffer[64];
  int idx = 0;

  // Header: last byte = headerType (0x02 = single, 0x03 = bank)
  buffer[idx++] = 0xF0;  // SysEx start
  buffer[idx++] = 0x42;  // Korg ID
  buffer[idx++] = 0x50;  // Model ID
  buffer[idx++] = 0x36;  // Device/channel
  buffer[idx++] = headerType;

  // Patch name (13 chars → 26 nibbles)
  char name[14];
  strncpy(name, patchName.c_str(), 13);
  name[13] = '\0';
  for (int i = 0; i < 13; i++) {
    byte c = (byte)name[i];
    buffer[idx++] = (c >> 4) & 0x0F;
    buffer[idx++] = c & 0x0F;
  }

  // Patch data (12 bytes → 24 nibbles)
  recallPatch(patchIndex);
  byte packed[12];
  encodePatch(patchIndex, packed);
  for (int i = 0; i < 12; i++) {
    buffer[idx++] = (packed[i] >> 4) & 0x0F;
    buffer[idx++] = packed[i] & 0x0F;
  }

  buffer[idx++] = 0xF7;  // SysEx end

  // Send SysEx over MIDI
  MIDI.sendSysEx(idx, buffer, true);
}

void sendSinglePatch(int patchIndex) {
  if (saveCurrent) {
    sendingSysEx = true;
    showCurrentParameterPage("Sending", String("Current Patch"));
    startParameterDisplay();
    recallPatch(patchIndex);
    sendPatchWithHeader(patchName, patchIndex, 0x02);  // headerType = 0x02
    saveCurrent = false;
    storeSaveCurrent(saveCurrent);
    settings::decrement_setting_value();
    settings::save_current_value();
    showSettingsPage();
    delay(100);
    sendingSysEx = false;
    state = PARAMETER;
    startParameterDisplay();
    recallPatchFlag = false;
    updateParams = true;
  }
}

void sendBankDump() {
  if (saveEditorAll) {
    sendingSysEx = true;
    sendingSysEx = true;
    showCurrentParameterPage("Sending", String("All Patches"));
    for (int p = 0; p < 80; p++) {
      recallPatch(p + 1);
      sendPatchWithHeader(patchName, p + 1, 0x03);  // headerType = 0x03
      delay(5);                                     // small pause between sends
    }
    saveEditorAll = false;
    storeSaveEditorAll(saveEditorAll);
    settings::decrement_setting_value();
    settings::save_current_value();
    showSettingsPage();
    delay(100);
    sendingSysEx = false;
    state = PARAMETER;
    recallPatch(1);
    startParameterDisplay();
    recallPatchFlag = false;
    updateParams = true;
  }
}

void myConvertControlChange(byte channel, byte number, byte value) {
  if (!recallPatchFlag) {
    switch (number) {

      case 0:
        MIDI.sendControlChange(number, value, midiOutCh);
        break;

      case 1:
        MIDI.sendControlChange(number, value, midiOutCh);
        break;

      case 2:
        MIDI.sendControlChange(number, value, midiOutCh);
        break;

      case 7:
        MIDI.sendControlChange(96, value, midiOutCh);
        break;

      case 64:
        MIDI.sendControlChange(number, value, midiOutCh);
        break;

      default:
        int newvalue = value;
        myControlChange(channel, number, newvalue);
        break;
    }
  }
}

void myPitchBend(byte channel, int bend) {
  if (!recallPatchFlag) {
    MIDI.sendPitchBend(bend, midiOutCh);
  }
}

void myAfterTouch(byte channel, byte value) {
  if (!recallPatchFlag) {
    if (afterTouch) {
      MIDI.sendControlChange(1, value, midiOutCh);
    }
  }
}

void allNotesOff() {
}

void updateosc1_PWM() {
  if (!recallPatchFlag) {
    if (osc1_pwm == 0) {
      showCurrentParameterPage("Osc1 PWM", String("Off"));
    } else {
      showCurrentParameterPage("Osc1 PWM", String(osc1_pwm));
    }
    startParameterDisplay();
  }
  midiCCOut(CCosc1_PWM, map(osc1_pwm, 0, 63, 0, 127));
}

void updateosc2_detune() {
  if (!recallPatchFlag) {
    if (osc2_detune == 1) {
      showCurrentParameterPage("Osc2 Detune", "Off");
    } else {
      showCurrentParameterPage("Osc2 Detune", String(osc2_detune));
    }
    startParameterDisplay();
  }
  midiCCOut(CCosc2_detune, map(osc2_detune, 1, 6, 0, 127));
}

void updatevcf_cutoff() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("VCF Cutoff", String(vcf_cutoff));
    startParameterDisplay();
  }
  midiCCOut(CCvcf_cutoff, map(vcf_cutoff, 0, 99, 0, 127));
}

void updatevcf_res() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("VCF Res", String(vcf_res));
    startParameterDisplay();
  }
  midiCCOut(CCvcf_res, map(vcf_res, 0, 99, 0, 127));
}

void updatevcf_eg_depth() {
  if (!recallPatchFlag) {
    if (vcf_eg_depth == 0) {
      showCurrentParameterPage("VCF EG Depth", "Off");
    } else {
      showCurrentParameterPage("VCF EG Depth", String(vcf_eg_depth));
    }
    startParameterDisplay();
  }
  midiCCOut(CCvcf_eg_depth, map(vcf_eg_depth, 0, 7, 0, 127));
}

void updatevcf_key_follow() {
  if (!recallPatchFlag) {
    if (vcf_key_follow == 0) {
      showCurrentParameterPage("VCF Keytrack", String("Off"));
    } else {
      showCurrentParameterPage("VCF Keytrack", String("On"));
    }
    startParameterDisplay();
  }
  switch (vcf_key_follow) {
    case 0:
      mcp2.digitalWrite(VCF_KEYTRACK_LED_RED, LOW);
      midiCCOut(CCvcf_key_follow, 0);
      break;
    case 1:
      mcp2.digitalWrite(VCF_KEYTRACK_LED_RED, HIGH);
      midiCCOut(CCvcf_key_follow, 64);
      break;
  }
}

void updatevca_gate() {
  if (!recallPatchFlag) {
    if (vca_gate == 0) {
      showCurrentParameterPage("VCA Type", String("Gated"));
    } else {
      showCurrentParameterPage("VCA Type", String("Envelope"));
    }
    startParameterDisplay();
  }
  switch (vca_gate) {
    case 0:
      mcp3.digitalWrite(VCA_ADSRLED_RED, LOW);
      midiCCOut(CCvca_gate, 0);
      break;
    case 1:
      mcp3.digitalWrite(VCA_ADSRLED_RED, HIGH);
      midiCCOut(CCvca_gate, 64);
      break;
  }
}

void updatekey_rotate() {
  if (!recallPatchFlag) {
    if (key_rotate == 0) {
      showCurrentParameterPage("Key Assign", String("Normal"));
    } else {
      showCurrentParameterPage("Key Assign", String("Rotate"));
    }
    startParameterDisplay();
  }
  switch (key_rotate) {
    case 0:
      mcp4.digitalWrite(KEY_ROTATE_LED_RED, HIGH);
      mcp4.digitalWrite(KEY_ROTATE_LED_GREEN, LOW);
      midiCCOut(CCkey_rotate, 0);
      break;
    case 1:
      mcp4.digitalWrite(KEY_ROTATE_LED_RED, LOW);
      mcp4.digitalWrite(KEY_ROTATE_LED_GREEN, HIGH);
      midiCCOut(CCkey_rotate, 64);
      break;
  }
}

void updatelfo1_speed() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Speed", String(lfo1_speed));
    startParameterDisplay();
  }
  midiCCOut(CClfo1_speed, map(lfo1_speed, 0, 41, 0, 127));
}

void updatelfo1_delay() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Delay", String(lfo1_delay));
    startParameterDisplay();
  }
  midiCCOut(CClfo1_delay, map(lfo1_delay, 0, 7, 0, 127));
}

void updatelfo1_vco() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 to DCO", String(lfo1_vco));
    startParameterDisplay();
  }
  midiCCOut(CCmodWheelinput, map(lfo1_vco, 0, 15, 0, 127));
}

void updatelfo1_vcf() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 to VCF", String(lfo1_vcf));
    startParameterDisplay();
  }
  midiCCOut(CClfo1_vcf, map(lfo1_vcf, 0, 15, 0, 127));
}

void updatelfo2_speed() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 Speed", String(lfo2_speed));
    startParameterDisplay();
  }
  midiCCOut(CClfo2_speed, map(lfo2_speed, 0, 31, 0, 127));
}

void updateeg1_attack() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("EG1 Attack", String(eg1_attack));
    startParameterDisplay();
  }
  midiCCOut(CCeg1_attack, map(eg1_attack, 0, 99, 0, 127));
}

void updateeg1_decay() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("EG1 Decay", String(eg1_decay));
    startParameterDisplay();
  }
  midiCCOut(CCeg1_decay, map(eg1_decay, 0, 99, 0, 127));
}

void updateeg1_release() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("EG1 Release", String(eg1_release));
    startParameterDisplay();
  }
  midiCCOut(CCeg1_release, map(eg1_release, 0, 99, 0, 127));
}

void updateeg1_sustain() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("EG1 Sustain", String(eg1_sustain));
    startParameterDisplay();
  }
  midiCCOut(CCeg1_sustain, map(eg1_sustain, 0, 99, 0, 127));
}

// Buttons

void updateosc1_octave() {
  if (!recallPatchFlag) {
    switch (osc1_octave) {
      case 0:
        showCurrentParameterPage("Osc1 Octave", String("16 Foot"));
        break;
      case 1:
        showCurrentParameterPage("Osc1 Octave", String("8 Foot"));
        break;
      case 2:
        showCurrentParameterPage("Osc1 Octave", String("4 Foot"));
        break;
    }
    startParameterDisplay();
  }
  switch (osc1_octave) {
    case 0:
      mcp1.digitalWrite(OSC1_OCTAVE_LED_RED, HIGH);
      mcp1.digitalWrite(OSC1_OCTAVE_LED_GREEN, LOW);
      midiCCOut(CCosc1_octave, 0);
      break;
    case 1:
      mcp1.digitalWrite(OSC1_OCTAVE_LED_RED, HIGH);
      mcp1.digitalWrite(OSC1_OCTAVE_LED_GREEN, HIGH);
      midiCCOut(CCosc1_octave, 43);
      break;
    case 2:
      mcp1.digitalWrite(OSC1_OCTAVE_LED_RED, LOW);
      mcp1.digitalWrite(OSC1_OCTAVE_LED_GREEN, HIGH);
      midiCCOut(CCosc1_octave, 86);
      break;
  }
}

void updateosc2_octave() {
  if (!recallPatchFlag) {
    switch (osc2_octave) {
      case 0:
        showCurrentParameterPage("Osc2 Octave", String("16 Foot"));
        break;
      case 1:
        showCurrentParameterPage("Osc2 Octave", String("8 Foot"));
        break;
      case 2:
        showCurrentParameterPage("Osc2 Octave", String("4 Foot"));
        break;
    }
    startParameterDisplay();
  }
  switch (osc2_octave) {
    case 0:
      mcp2.digitalWrite(OSC2_OCTAVE_LED_RED, HIGH);
      mcp2.digitalWrite(OSC2_OCTAVE_LED_GREEN, LOW);
      midiCCOut(CCosc2_octave, 0);
      break;
    case 1:
      mcp2.digitalWrite(OSC2_OCTAVE_LED_RED, HIGH);
      mcp2.digitalWrite(OSC2_OCTAVE_LED_GREEN, HIGH);
      midiCCOut(CCosc2_octave, 43);
      break;
    case 2:
      mcp2.digitalWrite(OSC2_OCTAVE_LED_RED, LOW);
      mcp2.digitalWrite(OSC2_OCTAVE_LED_GREEN, HIGH);
      midiCCOut(CCosc2_octave, 86);
      break;
  }
}

void updateosc1_wave() {
  if (!recallPatchFlag) {
    switch (osc1_wave) {
      case 0:
        showCurrentParameterPage("Osc1 Wave", String("Off"));
        break;
      case 1:
        showCurrentParameterPage("Osc1 Wave", String("Sawtooth"));
        break;
      case 2:
        showCurrentParameterPage("Osc1 Wave", String("Pulse"));
        break;
      case 3:
        showCurrentParameterPage("Osc1 Wave", String("PWM"));
        break;
    }
    startParameterDisplay();
  }
  switch (osc1_wave) {
    case 0:
      midiCCOut(CCosc1_wave, 0);
      break;
    case 1:
      midiCCOut(CCosc1_wave, 32);
      break;
    case 2:
      midiCCOut(CCosc1_wave, 64);
      break;
    case 3:
      midiCCOut(CCosc1_wave, 96);
      break;
  }
}

void updateosc2_wave() {
  if (!recallPatchFlag) {
    switch (osc2_wave) {
      case 0:
        showCurrentParameterPage("Osc2 Wave", String("Off"));
        break;
      case 1:
        showCurrentParameterPage("Osc2 Wave", String("Sawtooth"));
        break;
      case 2:
        showCurrentParameterPage("Osc2 Wave", String("Pulse"));
        break;
      case 3:
        showCurrentParameterPage("Osc2 Wave", String("New 1"));
        break;
      case 4:
        showCurrentParameterPage("Osc2 Wave", String("New 2"));
        break;
      case 5:
        showCurrentParameterPage("Osc2 Wave", String("New 3"));
        break;
      case 6:
        showCurrentParameterPage("Osc2 Wave", String("New 4"));
        break;
      case 7:
        showCurrentParameterPage("Osc2 Wave", String("New 5"));
        break;
    }
    startParameterDisplay();
  }
  switch (osc2_wave) {
    case 0:
      midiCCOut(CCosc2_wave, 0);
      break;
    case 1:
      midiCCOut(CCosc2_wave, 16);
      break;
    case 2:
      midiCCOut(CCosc2_wave, 32);
      break;
    case 3:
      midiCCOut(CCosc2_wave, 48);
      break;
    case 4:
      midiCCOut(CCosc2_wave, 64);
      break;
    case 5:
      midiCCOut(CCosc2_wave, 80);
      break;
    case 6:
      midiCCOut(CCosc2_wave, 96);
      break;
    case 7:
      midiCCOut(CCosc2_wave, 112);
      break;
  }
}

void updateosc2_interval() {
  if (!recallPatchFlag) {
    if (osc2_interval == 0) {
      showCurrentParameterPage("Osc2 Interval", String("Off"));
    } else {
      showCurrentParameterPage("Osc2 Interval", String(osc2_interval));
    }
    startParameterDisplay();
  }
  midiCCOut(CCosc2_interval, map(osc2_interval, 0, 12, 0, 127));
}

void updatelfo1_wave() {
  if (!recallPatchFlag) {
    switch (lfo1_wave) {
      case 0:
        showCurrentParameterPage("LFO1 Wave", String("Triangle"));
        break;
      case 1:
        showCurrentParameterPage("LFO1 Wave", String("Ramp Up"));
        break;
      case 2:
        showCurrentParameterPage("LFO1 Wave", String("Ramp Down"));
        break;
      case 3:
        showCurrentParameterPage("LFO1 Wave", String("Square"));
        break;
      case 4:
        showCurrentParameterPage("LFO1 Wave", String("Random"));
        break;
    }
    startParameterDisplay();
  }

  switch (lfo1_wave) {
    case 0:
      midiCCOut(CClfo1_wave, 0);
      break;
    case 1:
      midiCCOut(CClfo1_wave, 26);
      break;
    case 2:
      midiCCOut(CClfo1_wave, 52);
      break;
    case 3:
      midiCCOut(CClfo1_wave, 77);
      break;
    case 4:
      midiCCOut(CClfo1_wave, 103);
      break;
  }
}

void updatelfo2_wave() {
  if (!recallPatchFlag) {
    switch (lfo2_wave) {
      case 0:
        showCurrentParameterPage("LFO2 Wave", String("Triangle"));
        break;
      case 1:
        showCurrentParameterPage("LFO2 Wave", String("Ramp Up"));
        break;
      case 2:
        showCurrentParameterPage("LFO2 Wave", String("Ramp Down"));
        break;
      case 3:
        showCurrentParameterPage("LFO2 Wave", String("Square"));
        break;
    }
    startParameterDisplay();
  }

  switch (lfo2_wave) {
    case 0:
      midiCCOut(CClfo2_wave, 0);
      break;
    case 1:
      midiCCOut(CClfo2_wave, 32);
      break;
    case 2:
      midiCCOut(CClfo2_wave, 64);
      break;
    case 3:
      midiCCOut(CClfo2_wave, 96);
      break;
  }
}

void updatelfo_src() {
  if (!recallPatchFlag) {
    switch (lfo_src) {
      case 0:
        showCurrentParameterPage("LFO Source", String("LFO 1"));
        break;
      case 1:
        showCurrentParameterPage("LFO Source", String("LFO 2"));
        break;
    }
    startParameterDisplay();
  }
  switch (lfo_src) {
    case 0:
      mcp4.digitalWrite(LFO2_SRC_LED_RED, HIGH);
      mcp4.digitalWrite(LFO2_SRC_LED_GREEN, LOW);
      midiCCOut(CClfo_src, 0);
      break;
    case 1:
      mcp4.digitalWrite(LFO2_SRC_LED_RED, LOW);
      mcp4.digitalWrite(LFO2_SRC_LED_GREEN, HIGH);
      midiCCOut(CClfo_src, 64);
      break;
  }
}

void startParameterDisplay() {
  refreshScreen();

  lastDisplayTriggerTime = millis();
  waitingToUpdate = true;
}

void updatePatchname() {
  showPatchPage(String(patchNo), patchName);
}

void myControlChange(byte channel, byte control, int value) {
  switch (control) {

    case CCosc1_PWM:
      osc1_pwm = map(value, 0, 127, 0, 63);
      updateosc1_PWM();
      break;

    case CCosc2_interval:
      osc2_interval = map(value, 0, 127, 0, 12);
      updateosc2_interval();
      break;

    case CCosc2_detune:
      osc2_detune = map(value, 0, 127, 1, 6);
      updateosc2_detune();
      break;

    case CCvcf_cutoff:
      vcf_cutoff = map(value, 0, 127, 0, 99);
      updatevcf_cutoff();
      break;

    case CCvcf_res:
      vcf_res = map(value, 0, 127, 0, 99);
      updatevcf_res();
      break;

    case CCvcf_eg_depth:
      vcf_eg_depth = map(value, 0, 127, 0, 7);
      updatevcf_eg_depth();
      break;

    case CCvcf_key_follow:
      switch (value) {
        case 0 ... 63:
          vcf_key_follow = 0;
          break;
        case 64 ... 127:
          vcf_key_follow = 1;
          break;
      }
      updatevcf_key_follow();
      break;

    case CCvca_gate:
      switch (value) {
        case 0 ... 63:
          vca_gate = 0;
          break;
        case 64 ... 127:
          vca_gate = 1;
          break;
      }
      updatevca_gate();
      break;

    case CCkey_rotate:
      switch (value) {
        case 0 ... 63:
          key_rotate = 0;
          break;
        case 64 ... 127:
          key_rotate = 1;
          break;
      }
      updatekey_rotate();
      break;

    case CClfo1_speed:
      lfo1_speed = map(value, 0, 127, 0, 41);
      updatelfo1_speed();
      break;

    case CClfo1_delay:
      lfo1_delay = map(value, 0, 127, 0, 7);
      updatelfo1_delay();
      break;

    case CCmodWheelinput:
      lfo1_vco = map(value, 0, 127, 0, 15);
      updatelfo1_vco();
      break;

    case CClfo1_vcf:
      lfo1_vcf = map(value, 0, 127, 0, 15);
      updatelfo1_vcf();
      break;

    case CClfo2_speed:
      lfo2_speed = map(value, 0, 127, 0, 31);
      updatelfo2_speed();
      break;

    case CCeg1_attack:
      eg1_attack = map(value, 0, 127, 0, 99);
      updateeg1_attack();
      break;

    case CCeg1_decay:
      eg1_decay = map(value, 0, 127, 0, 99);
      updateeg1_decay();
      break;

    case CCeg1_release:
      eg1_release = map(value, 0, 127, 0, 99);
      updateeg1_release();
      break;

    case CCeg1_sustain:
      eg1_sustain = map(value, 0, 127, 0, 99);
      updateeg1_sustain();
      break;

    case CCosc1_octave:
      switch (value) {
        case 0 ... 42:
          osc1_octave = 0;
          break;
        case 43 ... 85:
          osc1_octave = 1;
          break;
        case 86 ... 127:
          osc1_octave = 2;
          break;
      }
      updateosc1_octave();
      break;

    case CCosc2_octave:
      switch (value) {
        case 0 ... 42:
          osc2_octave = 0;
          break;
        case 43 ... 85:
          osc2_octave = 1;
          break;
        case 86 ... 127:
          osc2_octave = 2;
          break;
      }
      updateosc2_octave();
      break;

    case CCosc1_wave:
      switch (value) {
        case 0 ... 31:
          osc1_wave = 0;
          break;
        case 32 ... 63:
          osc1_wave = 1;
          break;
        case 64 ... 95:
          osc1_wave = 2;
          break;
        case 96 ... 127:
          osc1_wave = 3;
          break;
      }
      updateosc1_wave();
      break;

    case CCosc2_wave:
      switch (value) {
        case 0 ... 15:
          osc2_wave = 0;
          break;
        case 16 ... 31:
          osc2_wave = 1;
          break;
        case 32 ... 47:
          osc2_wave = 2;
          break;
        case 48 ... 63:
          osc2_wave = 3;
          break;
        case 64 ... 79:
          osc2_wave = 4;
          break;
        case 80 ... 95:
          osc2_wave = 5;
          break;
        case 96 ... 111:
          osc2_wave = 6;
          break;
        case 112 ... 127:
          osc2_wave = 7;
          break;
      }
      updateosc2_wave();
      break;


    case CClfo1_wave:
      switch (value) {
        case 0 ... 25:
          lfo1_wave = 0;
          break;
        case 26 ... 51:
          lfo1_wave = 1;
          break;
        case 52 ... 76:
          lfo1_wave = 2;
          break;
        case 77 ... 102:
          lfo1_wave = 3;
          break;
        case 103 ... 127:
          lfo1_wave = 4;
          break;
      }
      updatelfo1_wave();
      break;

    case CClfo2_wave:
      switch (value) {
        case 0 ... 31:
          lfo2_wave = 0;
          break;
        case 32 ... 63:
          lfo2_wave = 1;
          break;
        case 64 ... 95:
          lfo2_wave = 2;
          break;
        case 96 ... 127:
          lfo2_wave = 3;
          break;
      }
      updatelfo2_wave();
      break;

    case CClfo_src:
      switch (value) {
        case 0 ... 63:
          lfo_src = 0;
          break;
        case 64 ... 127:
          lfo_src = 1;
          break;
      }
      updatelfo_src();
      break;

    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program) {
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  //Serial.print("MIDI Pgm Change:");
  //Serial.println(patchNo);
  state = PARAMETER;
}

void recallPatch(int patchNo) {
  allNotesOff();

  if (!sendingSysEx) {
    if (!updateParams) {
      MIDI.sendProgramChange(patchNo - 1, midiOutCh);
    }
  }

  delay(50);  // Let synth catch up
  recallPatchFlag = true;

  // Format filename without zero-padding
  char filename[16];
  snprintf(filename, sizeof(filename), "/%d", patchNo);  // e.g., "/1", "/2"

  //Serial.print("Loading patch file: ");
  //Serial.println(filename);

  File patchFile = SD.open(filename);
  if (!patchFile) {
    //Serial.print("Patch file not found: ");
    //Serial.println(filename);
  } else {
    String data[NO_OF_PARAMS];
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
    //Serial.println("Patch data loaded successfully.");
  }

  recallPatchFlag = false;
}


void setCurrentPatchData(String data[]) {
  patchName = data[0];
  osc1_octave = data[1].toInt();
  osc1_wave = data[2].toInt();
  osc1_pwm = data[3].toInt();
  vca_gate = data[4].toInt();
  osc2_octave = data[5].toInt();
  osc2_detune = data[6].toInt();
  osc2_wave = data[7].toInt();
  osc2_interval = data[8].toInt();
  vcf_cutoff = data[9].toInt();
  vcf_res = data[10].toInt();
  vcf_eg_depth = data[11].toInt();
  vcf_key_follow = data[12].toInt();
  lfo1_speed = data[13].toInt();
  lfo1_delay = data[14].toInt();
  lfo1_wave = data[15].toInt();
  lfo_src = data[16].toInt();
  eg1_attack = data[17].toInt();
  eg1_decay = data[18].toInt();
  eg1_sustain = data[19].toInt();
  eg1_release = data[20].toInt();
  lfo2_speed = data[21].toInt();
  lfo2_wave = data[22].toInt();
  key_rotate = data[23].toInt();
  lfo1_vcf = data[24].toInt();
  lfo1_vco = data[25].toInt();

  //Patchname
  updatePatchname();

  //Serial.print("Set Patch: ");
  //Serial.println(patchName);
  if (!sendingSysEx) {
    if (updateParams) {
      sendToSynthData();
    }
  }
}

void sendToSynthData() {

  updateosc1_octave();
  updateosc1_wave();
  updateosc1_PWM();
  updateosc2_octave();
  updateosc2_detune();
  updateosc2_wave();
  updateosc2_interval();
  updatevcf_cutoff();
  updatevcf_res();
  updatevcf_eg_depth();
  updatevcf_key_follow();
  updatelfo1_speed();
  updatelfo1_delay();
  updatelfo1_wave();
  updatelfo2_speed();
  updatelfo2_wave();
  updatelfo_src();
  updatelfo1_vco();
  updatelfo1_vcf();
  updateeg1_attack();
  updateeg1_decay();
  updateeg1_sustain();
  updateeg1_release();
  updatevca_gate();
  updatekey_rotate();
}

String getCurrentPatchData() {
  return patchName + "," + String(osc1_octave) + "," + String(osc1_wave) + "," + String(osc1_pwm) + "," + String(vca_gate)
         + "," + String(osc2_octave) + "," + String(osc2_detune) + "," + String(osc2_wave) + "," + String(osc2_interval)
         + "," + String(vcf_cutoff) + "," + String(vcf_res) + "," + String(vcf_eg_depth) + "," + String(vcf_key_follow)
         + "," + String(lfo1_speed) + "," + String(lfo1_delay) + "," + String(lfo1_wave) + "," + String(lfo_src)
         + "," + String(eg1_attack) + "," + String(eg1_decay) + "," + String(eg1_sustain) + "," + String(eg1_release)
         + "," + String(lfo2_speed) + "," + String(lfo2_wave) + "," + String(key_rotate) + "," + String(lfo1_vcf) + "," + String(lfo1_vco);
}

void showSettingsPage() {
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

void midiCCOut(byte cc, byte value) {
  MIDI.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
  if (updateParams) {
    delay(3);
  }
}

void checkSwitches() {

  saveButton.update();
  if (saveButton.held()) {
    switch (state) {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        break;
    }
    refreshScreen();
  } else if (saveButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        if (patches.size() < PATCHES_LIMIT) {
          resetPatchesOrdering();  //Reset order of patches from first patch
          patches.push({ patches.size() + 1, INITPATCHNAME });
          state = SAVE;
        }
        refreshScreen();
        break;
      case SAVE:
        //Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
        patchName = patches.last().patchName;
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(String(patches.last().patchNo), patches.last().patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        refreshScreen();
        break;
      case PATCHNAMING:
        if (renamedPatch.length() > 0) patchName = renamedPatch;  //Prevent empty strings
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(String(patches.last().patchNo), patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        refreshScreen();
        break;
    }
  }

  settingsButton.update();
  if (settingsButton.held()) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
    refreshScreen();
  } else if (settingsButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = SETTINGS;
        showSettingsPage();
        refreshScreen();
        break;
      case SETTINGS:
        showSettingsPage();
        refreshScreen();
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        refreshScreen();
        break;
    }
  }

  backButton.update();
  if (backButton.held()) {
    //If Back button held, Panic - all notes off
  } else if (backButton.numClicks() == 1) {
    switch (state) {
      case RECALL:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        refreshScreen();
        break;
      case SAVE:
        renamedPatch = "";
        state = PARAMETER;
        loadPatches();  //Remove patch that was to be saved
        setPatchesOrdering(patchNo);
        refreshScreen();
        break;
      case PATCHNAMING:
        charIndex = 0;
        renamedPatch = "";
        state = SAVE;
        refreshScreen();
        break;
      case DELETE:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        refreshScreen();
        break;
      case SETTINGS:
        state = PARAMETER;
        refreshScreen();
        break;
      case SETTINGSVALUE:
        state = SETTINGS;
        showSettingsPage();
        refreshScreen();
        break;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.held()) {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
    refreshScreen();
  } else if (recallButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = RECALL;  //show patch list
        refreshScreen();
        break;
      case RECALL:
        state = PATCH;
        //Recall the current patch
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        refreshScreen();
        break;
      case SAVE:
        showRenamingPage(patches.last().patchName);
        patchName = patches.last().patchName;
        state = PATCHNAMING;
        refreshScreen();
        break;
      case PATCHNAMING:
        if (renamedPatch.length() < 12)  //actually 12 chars
        {
          renamedPatch.concat(String(currentCharacter));
          charIndex = 0;
          currentCharacter = CHARACTERS[charIndex];
          showRenamingPage(renamedPatch);
        }
        refreshScreen();
        break;
      case DELETE:
        //Don't delete final patch
        if (patches.size() > 1) {
          state = DELETEMSG;
          patchNo = patches.first().patchNo;  //PatchNo to delete from SD card
          patches.shift();                    //Remove patch from circular buffer
          deletePatch(patchNo);               //Delete from SD card
          loadPatches();                      //Repopulate circular buffer to start from lowest Patch No
          renumberPatchesOnSD();
          loadPatches();                      //Repopulate circular buffer again after delete
          patchNo = patches.first().patchNo;  //Go back to 1
          recallPatch(patchNo);               //Load first patch
        }
        state = PARAMETER;
        refreshScreen();
        break;
      case SETTINGS:
        state = SETTINGSVALUE;
        showSettingsPage();
        refreshScreen();
        break;
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        refreshScreen();
        break;
    }
  }
}

void reinitialiseToPanel() {

  osc1_octave = 1;
  osc1_wave = 1;
  osc1_pwm = 0;

  osc2_octave = 1;
  osc2_detune = 1;
  osc2_wave = 1;

  vcf_cutoff = 99;
  vcf_res = 0;
  vcf_eg_depth = 7;
  vcf_key_follow = 0;

  lfo1_speed = 10;
  lfo1_delay = 0;
  lfo1_wave = 0;
  lfo_src = 0;
  lfo1_vco = 0;
  lfo1_vcf = 0;

  eg1_attack = 0;
  eg1_decay = 0;
  eg1_sustain = 99;
  eg1_release = 0;

  lfo2_speed = 15;
  lfo2_wave = 0;
  vca_gate = 1;
  key_rotate = 0;

  recallPatchFlag = true;
  sendToSynthData();
  recallPatchFlag = false;
}

void checkEncoder() {
  long encRead = encoder.getCount();

  bool movedUp = (encCW && encRead > encPrevious + 1) || (!encCW && encRead < encPrevious - 1);
  bool movedDown = (encCW && encRead < encPrevious - 1) || (!encCW && encRead > encPrevious + 1);

  if (movedUp || movedDown) {
    bool goingUp = movedUp;

    switch (state) {
      case PARAMETER:
        if (!patches.isEmpty()) {
          state = PATCH;

          if (goingUp) {
            patches.push(patches.shift());
          } else {
            patches.unshift(patches.pop());
          }

          patchNo = patches.first().patchNo;
          if (patchNo > 0) {
            //Serial.printf("Recalling patch #%d from encoder\n", patchNo);
            recallPatch(patchNo);
          } else {
            //Serial.println("⚠️ Invalid patchNo == 0, skipping recall.");
          }

          state = PARAMETER;
          refreshScreen();
        } else {
          //Serial.println("⚠️ patches buffer is empty in PARAMETER state!");
        }
        break;

      case RECALL:
      case SAVE:
      case DELETE:
        if (!patches.isEmpty()) {
          if (goingUp) {
            patches.push(patches.shift());
          } else {
            patches.unshift(patches.pop());
          }
          refreshScreen();
        }
        break;

      case PATCHNAMING:
        if (goingUp) {
          if (++charIndex >= TOTALCHARS) charIndex = 0;
        } else {
          if (--charIndex < 0) charIndex = TOTALCHARS - 1;
        }
        currentCharacter = CHARACTERS[charIndex];
        showRenamingPage(renamedPatch + currentCharacter);
        refreshScreen();
        break;

      case SETTINGS:
        if (goingUp)
          settings::increment_setting();
        else
          settings::decrement_setting();
        showSettingsPage();
        refreshScreen();
        break;

      case SETTINGSVALUE:
        if (goingUp)
          settings::increment_setting_value();
        else
          settings::decrement_setting_value();
        showSettingsPage();
        refreshScreen();
        break;
    }

    encPrevious = encRead;
  }
}

void checkLoadFactory() {
  if (loadFactory) {
    showCurrentParameterPage("Loading", String("Factory Patch"));
    startParameterDisplay();
    for (int row = 0; row < 80; row++) {
      String name;
      uint8_t patchBytes[12];

      // Convert one factory patch string into name + 12 bytes
      parseFactoryPatch(row, name, patchBytes);

      // Decode into synth parameters
      decodePatch(patchBytes);

      patchName = name;  // Store name in slot 0}

      sprintf(buffer, "%d", row + 1);
      savePatch(buffer, getCurrentPatchData());
      updatePatchname();
      //Serial.printf("Factory patch %02d saved as %s\n", row + 1, name.c_str());
    }

    loadPatches();  // Refresh patch list
    loadFactory = false;
    storeLoadFactory(loadFactory);

    // Reset state
    settings::decrement_setting_value();
    settings::save_current_value();
    showSettingsPage();
    delay(100);
    state = PARAMETER;
    recallPatch(1);
    startParameterDisplay();
  }
}

void parseFactoryPatch(int row, String &name, uint8_t patchBytes[12]) {
  // Get one row of the factorynibbles table
  String line = factorynibbles[row];

  // Split by commas
  int valuesCount = 0;
  String parts[30];  // plenty of room (1 name + 24 nibbles)

  int start = 0;
  for (int i = 0; i < line.length(); i++) {
    if (line.charAt(i) == ',') {
      parts[valuesCount++] = line.substring(start, i);
      start = i + 1;
    }
  }
  // Add last value
  if (start < line.length()) {
    parts[valuesCount++] = line.substring(start);
  }

  // First entry = patch name (trim spaces)
  name = parts[0];
  name.trim();

  // Next 24 entries = nibbles
  uint8_t nibbles[24];
  for (int i = 0; i < 24; i++) {
    String s = parts[i + 1];
    s.trim();
    nibbles[i] = (uint8_t)strtol(s.c_str(), NULL, 16);  // parse hex
  }

  // Pack nibbles into 12 bytes
  for (int b = 0; b < 12; b++) {
    patchBytes[b] = ((nibbles[b * 2] & 0x0F) << 4) | (nibbles[b * 2 + 1] & 0x0F);
  }
}

void RotaryEncoderChanged(bool clockwise, int id) {

  if (!accelerate) {
    speed = 1;
  } else {
    speed = getEncoderSpeed(id);
  }

  // Serial.print("Encoder ");
  // Serial.println(id);

  if (!clockwise) {
    speed = -speed;
  }

  switch (id) {
    case 1:
      osc2_wave = (osc2_wave + speed);
      osc2_wave = constrain(osc2_wave, 0, 7);
      updateosc2_wave();
      break;

    case 2:
      osc1_pwm = (osc1_pwm + speed);
      osc1_pwm = constrain(osc1_pwm, 0, 63);
      updateosc1_PWM();
      break;

    case 3:
      osc1_wave = (osc1_wave + speed);
      osc1_wave = constrain(osc1_wave, 0, 3);
      updateosc1_wave();
      break;

    case 4:
      osc2_interval = (osc2_interval + speed);
      osc2_interval = constrain(osc2_interval, 0, 12);
      updateosc2_interval();
      break;

    case 5:
      osc2_detune = (osc2_detune + speed);
      osc2_detune = constrain(osc2_detune, 1, 6);
      updateosc2_detune();
      break;

    case 6:
      vcf_cutoff = (vcf_cutoff + speed);
      vcf_cutoff = constrain(vcf_cutoff, 0, 99);
      updatevcf_cutoff();
      break;

    case 7:
      eg1_attack = (eg1_attack + speed);
      eg1_attack = constrain(eg1_attack, 0, 99);
      updateeg1_attack();
      break;

    case 8:
      vcf_eg_depth = (vcf_eg_depth + speed);
      vcf_eg_depth = constrain(vcf_eg_depth, 0, 7);
      updatevcf_eg_depth();
      break;

    case 9:
      vcf_res = (vcf_res + speed);
      vcf_res = constrain(vcf_res, 0, 99);
      updatevcf_res();
      break;

    case 10:
      eg1_decay = (eg1_decay + speed);
      eg1_decay = constrain(eg1_decay, 0, 99);
      updateeg1_decay();
      break;

    case 11:
      eg1_sustain = (eg1_sustain + speed);
      eg1_sustain = constrain(eg1_sustain, 0, 99);
      updateeg1_sustain();
      break;

    case 12:
      eg1_release = (eg1_release + speed);
      eg1_release = constrain(eg1_release, 0, 99);
      updateeg1_release();
      break;

    case 13:
      lfo1_speed = (lfo1_speed + speed);
      lfo1_speed = constrain(lfo1_speed, 0, 41);
      updatelfo1_speed();
      break;

    case 14:
      lfo1_delay = (lfo1_delay + speed);
      lfo1_delay = constrain(lfo1_delay, 0, 7);
      updatelfo1_delay();
      break;

    case 15:
      lfo1_vco = (lfo1_vco + speed);
      lfo1_vco = constrain(lfo1_vco, 0, 15);
      updatelfo1_vco();
      break;

    case 16:
      lfo1_vcf = (lfo1_vcf + speed);
      lfo1_vcf = constrain(lfo1_vcf, 0, 15);
      updatelfo1_vcf();
      break;

    case 17:
      lfo1_wave = (lfo1_wave + speed);
      lfo1_wave = constrain(lfo1_wave, 0, 4);
      updatelfo1_wave();
      break;

    case 18:
      lfo2_speed = (lfo2_speed + speed);
      lfo2_speed = constrain(lfo2_speed, 0, 31);
      updatelfo2_speed();
      break;

    case 19:
      lfo2_wave = (lfo2_wave + speed);
      lfo2_wave = constrain(lfo2_wave, 0, 3);
      updatelfo2_wave();
      break;
  }


  //rotaryEncoderChanged(id, clockwise, speed);
}

int getEncoderSpeed(int id) {
  if (id < 1 || id > numEncoders) return 1;

  unsigned long now = millis();
  unsigned long revolutionTime = now - lastTransition[id];

  int speed = 1;
  if (revolutionTime < 50) {
    speed = 10;
  } else if (revolutionTime < 125) {
    speed = 5;
  } else if (revolutionTime < 250) {
    speed = 2;
  }

  lastTransition[id] = now;
  return speed;
}

void mainButtonChanged(Button *btn, bool released) {

  switch (btn->id) {
    case OSC1_OCT_BUTTON:
      if (!released) {
        osc1_octave = osc1_octave + 1;
        if (osc1_octave > 2) {
          osc1_octave = 0;
        }
        updateosc1_octave();
      }
      break;

    case OSC2_OCT_BUTTON:
      if (!released) {
        osc2_octave = osc2_octave + 1;
        if (osc2_octave > 2) {
          osc2_octave = 0;
        }
        updateosc2_octave();
      }
      break;

    case VCF_KEYTRACK_BUTTON:
      if (!released) {
        vcf_key_follow = !vcf_key_follow;
        updatevcf_key_follow();
      }
      break;

    case VCA_GATE_BUTTON:
      if (!released) {
        vca_gate = !vca_gate;
        updatevca_gate();
      }
      break;

    case LFO_SRC_BUTTON:
      if (!released) {
        lfo_src = !lfo_src;
        updatelfo_src();
      }
      break;

    case KEY_ROTATE_BUTTON:
      if (!released) {
        key_rotate = !key_rotate;
        updatekey_rotate();
      }
      break;
  }
}

// build the groups in setup()
void groupEncoders() {
  for (auto &enc : rotaryEncoders) {
    for (size_t i = 0; i < NUM_MCP; ++i) {
      if (enc.getMCP() == allMCPs[i]) {
        encByMCP[i].push_back(&enc);
        break;
      }
    }
  }
}

void pollAllMCPs() {

  for (int j = 0; j < numMCPs; j++) {
    uint16_t gpioAB = allMCPs[j]->readGPIOAB();
    for (int i = 0; i < numEncoders; i++) {
      if (rotaryEncoders[i].getMCP() == allMCPs[j])
        rotaryEncoders[i].feedInput(gpioAB);
    }

    for (auto &button : allButtons) {
      if (button->getMcp() == allMCPs[j]) {
        button->feedInput(gpioAB);
      }
    }
  }
}

void loop() {

  if (!recallPatchFlag) {
    MIDI.read(midiChannel);
  }

  if (!receivingSysEx) {

    pollAllMCPs();
    checkSwitches();
    checkEncoder();
    checkLoadFactory();
    sendSinglePatch(patchNo);
    sendBankDump();
    sendSysexDump();
  }

  if (waitingToUpdate && (millis() - lastDisplayTriggerTime >= displayTimeout)) {
    refreshScreen();  // retrigger
    waitingToUpdate = false;
  }

  if (sysexComplete) {
    sysexComplete = false;  // Reset for the next SysEx message
    showCurrentParameterPage("Processing", String("Sysex Dump"));
    startParameterDisplay();
    convertNibblesToBytes();
    decodePatches();
    currentBlock = 0;  // Reset to start filling from block 0 again
    byteIndex = 0;     // Reset byte index within the block
    receivingSysEx = false;
  }
}
