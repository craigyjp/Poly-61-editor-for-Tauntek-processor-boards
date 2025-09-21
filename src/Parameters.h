//Values below are just for initialising and will be changed when synth is initialised to current panel controls & EEPROM settings
byte midiChannel = MIDI_CHANNEL_OMNI;  //(EEPROM)
byte midiOutCh = 1;                    //(EEPROM)

// adding encoders
bool rotaryEncoderChanged(int id, bool clockwise, int speed);
#define NUM_ENCODERS 19
unsigned long lastTransition[NUM_ENCODERS + 1];
unsigned long lastDisplayTriggerTime = 0;
bool waitingToUpdate = false;
const unsigned long displayTimeout = 5000;  // e.g. 5 seconds

char buffer[10];

// Buffer for raw incoming SysEx (nibbles included)
byte sysexBuffer[HEADER_BYTES + (NUM_PATCHES * PATCH_NIBBLES) + 1];  
unsigned sysexIndex = 0;

// Final decoded patches (12 bytes each)
byte receivedPatches[NUM_PATCHES][PATCH_BYTES];
const byte expectedHeader[5] = {0xF0, 0x42, 0x50, 0x36, 0x31};
bool sendingSysEx = false;
bool receivingSysEx = false;
bool sysexComplete = false;
byte dumpType = 0;
int headerSkip = 0;
const int totalBytes = 1920;
int bankPatchCounter = 0;   // how many patches received in current bank
byte data(24);
byte sysexData[24];
#define SYSEX_BUFFER_SIZE 2048   // enough for 1926 bytes
#define HEADER_SIZE       5
#define PATCH_BLOCK_SIZE  24   // nibbles per patch
#define NUM_PATCHES       80

uint8_t ramArray[NUM_PATCHES][PATCH_BLOCK_SIZE];

int currentBlock = 0;
int byteIndex = 0;

int MIDIThru = midi::Thru::Off;  //(EEPROM)
String patchName = INITPATCHNAME;
String currentRow;
String bankdir = "/Bank";
boolean encCW = true;  //This is to set the encoder to increment when turned CW - Settings Option
long encPrevious = 0;
boolean recallPatchFlag = true;
boolean loadFactory = false;
boolean loadRAM = false;
boolean loadFromDW = false;
boolean ROMType = false;
boolean dataInProgress = false;
int currentSendPatch = 0;
boolean saveCurrent = false;
boolean afterTouch = false;
boolean saveAll = false;
boolean saveEditorAll = false;
byte accelerate = 1;
int speed = 1;
boolean updateParams = false;  //(EEPROM)
int bankselect = 0;
int old_value = 0;
int old_param_offset = 0;
int received_patch = 0;


int osc1_octave = 0;
int osc1_wave = 0;
int osc1_pwm = 0;

int osc2_octave = 0;
int osc2_detune = 0;
int osc2_interval = 0;
int osc2_wave = 0;

int vcf_cutoff = 0;
int vcf_res = 0;
int vcf_eg_depth = 0;
int vcf_key_follow = 0;

int lfo1_depth = 0;
int lfo1_speed = 0;
int lfo1_delay = 0;
int lfo1_wave = 0;
int lfo_select = 0;
int lfo_src = 0;
int lfo1_vco = 0;
int lfo1_vcf = 0;

int key_rotate = 0;

int eg1_attack = 0;
int eg1_decay = 0;
int eg1_sustain = 0;
int eg1_release = 0;

int vca_gate = 0;

int lfo2_speed = 0;
int lfo2_wave = 0;

int returnvalue = 0;
