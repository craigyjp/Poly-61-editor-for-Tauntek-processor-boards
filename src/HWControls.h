// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <ESP32Encoder.h>
#include "TButton.h"

#include "Rotary.h"
#include "RotaryEncOverMCP.h"

#define OSC1_OCT_BUTTON 0
#define OSC2_OCT_BUTTON 1
#define VCF_KEYTRACK_BUTTON 2
#define VCA_GATE_BUTTON 3
#define LFO_SRC_BUTTON 4
#define KEY_ROTATE_BUTTON 5

// Pins for MCP23017
#define GPA0 0
#define GPA1 1
#define GPA2 2
#define GPA3 3
#define GPA4 4
#define GPA5 5
#define GPA6 6
#define GPA7 7
#define GPB0 8
#define GPB1 9
#define GPB2 10
#define GPB3 11
#define GPB4 12
#define GPB5 13
#define GPB6 14
#define GPB7 15

void RotaryEncoderChanged (bool clockwise, int id);

void mainButtonChanged(Button *btn, bool released);

// I2C MCP23017 GPIO expanders

Adafruit_MCP23017 mcp1;
Adafruit_MCP23017 mcp2;
Adafruit_MCP23017 mcp3;
Adafruit_MCP23017 mcp4;

//Array of pointers of all MCPs
Adafruit_MCP23017 *allMCPs[] = {&mcp1, &mcp2, &mcp3, &mcp4};


// // My encoders
// /* Array of all rotary encoders and their pins */
RotaryEncOverMCP rotaryEncoders[] = {
        RotaryEncOverMCP(&mcp1, 0, 1, &RotaryEncoderChanged, 1),
        RotaryEncOverMCP(&mcp1, 2, 3, &RotaryEncoderChanged, 2),
        RotaryEncOverMCP(&mcp1, 4, 5, &RotaryEncoderChanged, 3),
        RotaryEncOverMCP(&mcp1, 8, 9, &RotaryEncoderChanged, 4),
        RotaryEncOverMCP(&mcp1, 10, 11, &RotaryEncoderChanged, 5),
        RotaryEncOverMCP(&mcp1, 12, 13, &RotaryEncoderChanged, 6),
        RotaryEncOverMCP(&mcp2, 0, 1, &RotaryEncoderChanged, 7),
        RotaryEncOverMCP(&mcp2, 2, 3, &RotaryEncoderChanged, 8),
        RotaryEncOverMCP(&mcp2, 4, 5, &RotaryEncoderChanged, 9),
        RotaryEncOverMCP(&mcp2, 9, 8, &RotaryEncoderChanged, 10),
        RotaryEncOverMCP(&mcp2, 10, 11, &RotaryEncoderChanged, 11),
        RotaryEncOverMCP(&mcp2, 12, 13, &RotaryEncoderChanged, 12),
        RotaryEncOverMCP(&mcp3, 0, 1, &RotaryEncoderChanged, 13),
        RotaryEncOverMCP(&mcp3, 2, 3, &RotaryEncoderChanged, 14),
        RotaryEncOverMCP(&mcp3, 4, 5, &RotaryEncoderChanged, 15),
        RotaryEncOverMCP(&mcp3, 8, 9, &RotaryEncoderChanged, 16),
        RotaryEncOverMCP(&mcp3, 10, 11, &RotaryEncoderChanged, 17),
        RotaryEncOverMCP(&mcp3, 12, 13, &RotaryEncoderChanged, 18),
        RotaryEncOverMCP(&mcp4, 0, 1, &RotaryEncoderChanged, 19),
};

// after your rotaryEncoders[] definition
constexpr size_t NUM_MCP = sizeof(allMCPs) / sizeof(allMCPs[0]);
constexpr int numMCPs = (int)(sizeof(allMCPs) / sizeof(*allMCPs));
constexpr int numEncoders = (int)(sizeof(rotaryEncoders) / sizeof(*rotaryEncoders));

// an array of vectors to hold pointers to the encoders on each MCP
std::vector<RotaryEncOverMCP*> encByMCP[NUM_MCP];

Button osc1_oct_Button = Button(&mcp1, 6, OSC1_OCT_BUTTON, &mainButtonChanged);
Button osc2_oct_Button = Button(&mcp1, 14, OSC2_OCT_BUTTON, &mainButtonChanged);
Button vcf_keytrack_Button = Button(&mcp2, 6, VCF_KEYTRACK_BUTTON, &mainButtonChanged);
Button vca_gate_Button = Button(&mcp3, 14, VCA_GATE_BUTTON, &mainButtonChanged);
Button lfo1_src_Button = Button(&mcp3, 6, LFO_SRC_BUTTON, &mainButtonChanged);
Button key_rotate_Button = Button(&mcp4, 9, KEY_ROTATE_BUTTON, &mainButtonChanged);

Button *mainButtons[] = {
        &osc1_oct_Button, &osc2_oct_Button, &vcf_keytrack_Button, &vca_gate_Button, &lfo1_src_Button, &key_rotate_Button
};

Button *allButtons[] = {
        &osc1_oct_Button, &osc2_oct_Button, &vcf_keytrack_Button, &vca_gate_Button, &lfo1_src_Button, &key_rotate_Button
};

// GP1
#define OSC1_OCTAVE_LED_RED 7
#define OSC1_OCTAVE_LED_GREEN 15

// GP2
#define OSC2_OCTAVE_LED_RED 7
#define VCF_KEYTRACK_LED_RED 14
#define OSC2_OCTAVE_LED_GREEN 15

// GP3

#define VCA_ADSRLED_RED 15

// GP4
#define LFO2_SRC_LED_RED 7
#define KEY_ROTATE_LED_RED 13
#define KEY_ROTATE_LED_GREEN 14
#define LFO2_SRC_LED_GREEN 15

//ESP32 Pins

#define RECALL_SW 27
#define BACK_SW 25
#define SAVE_SW 14
#define SETTINGS_SW 26


#define ENCODER_PINA 32
#define ENCODER_PINB 33

#define DEBOUNCE 30

//These are pushbuttons and require debouncing

TButton saveButton{ SAVE_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton settingsButton{ SETTINGS_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton backButton{ BACK_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton recallButton{ RECALL_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION }; // on encoder

ESP32Encoder encoder;

void setupHardware() {

  //Switches
  pinMode(RECALL_SW, INPUT_PULLUP);  //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);

}
