#include "application.h"
#include "calibration.h"
#include "hw.h"
#include "SPImcpDAC.h"
#include "ihandlers.h"
#include "timer.h"
#include "EEPROM.h"

// state machine
typedef enum { CALIBRATING = 0, PLAYING } theremin_state_t;
// storage state global var
theremin_state_t g_state = PLAYING;
// audio output state global var
bool g_audio_output_is_enabled = true;

static const uint16_t MAX_VOLUME = 4095;


#if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
const float MIDDLE_C = 261.6;
void playTone(float hz, uint16_t milliseconds = 500, uint8_t volume = 255) {
    const float HZ_SCALING_FACTOR = 2.09785;
    bool old_audio_state = g_audio_output_is_enabled;
    g_audio_output_is_enabled = true; // force emit the tone
    setWavetableSampleAdvance((uint16_t)(hz * HZ_SCALING_FACTOR));
    millitimer(milliseconds);
    g_audio_output_is_enabled = old_audio_state; // restore previous mute state
}
#endif

// UI parameters (touch button and potentiometers)
#define UI_BUTTON_LONG_PRESS_DURATION   60000
// Potentiometer variables, hysteresis and scaling
#define HYST_SCALE 0.95
static const int16_t pot_register_selection_hysteresis = 1024.0 / 3 * HYST_SCALE;   // only three position octave selection
static const int16_t pot_waveform_selection_hysteresis = 1024.0 / num_wavetables * HYST_SCALE;  // map the waveform selection poti depending on how many waveforms are being loaded in the DDS generator
static const int16_t pot_rf_virtual_field_adjust_hysteresis = 1024 / 64;    // reduce the volume and pitch field potis to 64 steps to slightly reduce audio jitter when moving them
int16_t pitchPotValue = 0, pitchPotValueL = 0;
int16_t volumePotValue = 0, volumePotValueL = 0;
int16_t registerPotValue = 0, registerPotValueL = 0;
int16_t wavePotValue = 0, wavePotValueL = 0;
uint8_t registerValue = 2; // octave register
uint8_t registerValueL = 2; // octave register compare

/**
 * @brief Read all the potentiometer position and update the changed values
 *        if the hysteresis constant have been surpassed.
 * 
 * @param force
 *        optional boolean flag to force the updates (at power-on)
 */
void ui_potis_read_all(bool force = false) {
    pitchPotValueL = analogRead(PITCH_POT);
    if (force || abs(pitchPotValue - pitchPotValueL) >= pot_rf_virtual_field_adjust_hysteresis) { 
        pitchPotValue = pitchPotValueL;
    }
    
    volumePotValueL = analogRead(VOLUME_POT);
    if (force || abs(volumePotValue - volumePotValueL) >= pot_rf_virtual_field_adjust_hysteresis) { 
        volumePotValue = volumePotValueL;
    }

    registerPotValueL = analogRead(REGISTER_SELECT_POT);
    if (force || abs(registerPotValue - registerPotValueL) >= pot_register_selection_hysteresis) { 
        registerPotValue = registerPotValueL;
        // register pot offset configuration:
        // Left = -1 octave, Center = 0, Right = +1 octave
        if (registerPotValue > pot_register_selection_hysteresis * 2) {
            registerValueL = 1;
            DEBUG_PRINTLN(F("OCT+1"));
        } else if (registerPotValue < pot_register_selection_hysteresis) {
            registerValueL = 3;
            DEBUG_PRINTLN(F("OCT-1"));
        } else {
            registerValueL = 2;
            DEBUG_PRINTLN(F("OCT+0"));
        }
        if (registerValueL != registerValue) {
            registerValue = registerValueL;
        }
    }

    wavePotValueL = analogRead(WAVE_SELECT_POT);
    if (force || abs(wavePotValue - wavePotValueL) >= pot_waveform_selection_hysteresis) {
        wavePotValue = wavePotValueL;
        // map 0–1023 to 0–(num_wavetables - 1)
        uint16_t scaled = ((uint32_t)wavePotValue * num_wavetables) / 1024;
        if (scaled >= num_wavetables) scaled = num_wavetables - 1;    // extra safety
        if (scaled != vWavetableSelector) {
            vWavetableSelector = scaled;
            DEBUG_PRINT(F("WAV="));DEBUG_PRINTLN(scaled);
        }
    }
}


void theremin_setup() {
    // initializes the serial port if needed to the correct speed
    // see "build.h" file
#if SERIAL_PORT_MODE & SERIAL_PORT_MODE_MIDI
    Serial.begin(SERIAL_SPEED_MIDI);
#elif SERIAL_PORT_MODE == SERIAL_PORT_MODE_DEBUG
    Serial.begin(SERIAL_SPEED_DEBUG);
#endif

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_BLUE_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);

    HW_LED_BLUE_OFF; HW_LED_RED_ON;

    // initialize potentiometer position at startup
    // force update for correct initialization
    ui_potis_read_all(true);
    calibration_read();

    // start the DDS wave generator
    ihInitialiseTimer();
    ihInitialiseInterrupts();
}

void theremin_loop() {
    int32_t pitch_v = 0, pitch_l = 0;   // Last value of pitch    (for filtering)
    int32_t vol_v = 0, vol_l = 0;       // Last value of volume (for filtering and for tracking)

    int16_t pitch_p = -1; 
    int32_t vol_p = -1;

    uint16_t tmpVolume;
    int16_t tmpPitch;

mloop: // Main loop avoiding the GCC "optimization"
    // continuosly read potentiometers and flag changed values
    ui_potis_read_all();

    if (g_state == PLAYING && HW_BUTTON_PRESSED) {
        resetTimer();
        g_state = CALIBRATING;
        g_audio_output_is_enabled = !g_audio_output_is_enabled;
        if (g_audio_output_is_enabled) {
            HW_LED_BLUE_OFF; HW_LED_RED_ON;
        } else {
            HW_LED_BLUE_ON; HW_LED_RED_OFF;
        }
    }

    if (g_state == CALIBRATING && HW_BUTTON_RELEASED) {
        g_state = PLAYING;
    }

    if (g_state == CALIBRATING && timerExpired(UI_BUTTON_LONG_PRESS_DURATION)) {
        // signal the player to prepare for calibration
        // uncomment the following lines to have a countdown blink
        // so that the user can move his/her hands away from the antennas
        // TODO: update user manual if this function is to be included
        //       in production.
        // for (int i = 0; i<10; i++) {
        //    millitimer(200 - (i * 10));
        //    HW_LED_BLUE_TOGGLE; HW_LED_RED_TOGGLE;
        // }
        // both LEDs indicate calibration mode
        HW_LED_BLUE_ON; HW_LED_RED_ON;
        // flag audio as disabled during calibration for coherence
        // even though the interrupt handlers will be disabled
        g_audio_output_is_enabled = false;

        #if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
            playTone(MIDDLE_C, 150, 25);
            playTone(MIDDLE_C * 2, 150, 25);
            playTone(MIDDLE_C * 4, 150, 25);
        #endif

        bool success = calibration_start();
        if (success) {
            //HW_LED_BLUE_ON; HW_LED_RED_OFF;
            g_audio_output_is_enabled = true;
            #if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
                playTone(MIDDLE_C * 2, 150, 25);
                playTone(MIDDLE_C * 2, 150, 25);
            #endif
        } else {
            // visual feedback for failed calibration
            // please uncomment the following lines
            // to include this code in production
            // HW_LED_BLUE_OFF;
            // for (int i = 0; i<10; i++) {
            //     millitimer(200 - (i * 10));
            //     HW_LED_RED_TOGGLE;
            // }
            HW_LED_RED_ON;
            g_audio_output_is_enabled = true;
            #if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
                playTone(MIDDLE_C * 4, 150, 25);
                playTone(MIDDLE_C, 150, 25);
            #endif
        }
        g_state = PLAYING;
    }

#if SERIAL_PORT_MODE == SERIAL_PORT_MODE_MIDI
    // MIDI note events are sent out only if enabled in "build.h"
    if (timerExpired(TICKS_100_MILLIS)) {
        resetTimer();
        Serial.write(pitch & 0xff);
        Serial.write((pitch >> 8) & 0xff);
    }
#endif

    if (pitchValueAvailable) { 
        // If capture event
        pitch_p = pitch;
        pitch_v = pitch; // Averaging pitch values
        pitch_v = pitch_l + ((pitch_v - pitch_l) >> 2);
        pitch_l = pitch_v;

        // set wave frequency for each mode
        if (g_audio_output_is_enabled) {
            // if playing and not mute
            tmpPitch = ((pitchCalibrationBase - pitch_v) + 2048 - (pitchPotValue << 2));
            tmpPitch = min(tmpPitch, 16383);    // Unaudible upper limit just to prevent DAC overflow
            tmpPitch = max(tmpPitch, 0);            // Silence behing zero beat
            setWavetableSampleAdvance(tmpPitch >> registerValue);
            if (tmpPitch != pitch_p) { 
                pitch_p = tmpPitch;
#if CV_OUTPUT_MODE == CV_OUTPUT_MODE_LOG
                    // output new pitch CV value only if pitch value changed (saves runtime resources)
                    // 1V/Oct for Moog & Roland
                    uint16_t tmpOct = 0;
                    while (tmpPitch > 1023) {
                        tmpOct += 819;
                        tmpPitch >>= 1;
                    }
                    tmpPitch -= 512;
                    tmpPitch = max(tmpPitch, 0);
                    uint16_t tmpLog = (((uint32_t)tmpPitch * 819) >> 9);
                    pitchCV = (tmpOct + tmpLog) - 48;
                    pitchCV = max(pitchCV, 0);
                    pitchCVAvailable = true;
#elif CV_OUTPUT_MODE == CV_OUTPUT_MODE_LINEAR
                    // 819Hz/V for Korg & Yamaha
                    pitchCV = tmpPitch >> 2;
                    pitchCVAvailable = true;
#endif // CV OUT IF ENABLED
            }
        }
        pitchValueAvailable = false;
    }

    if (volumeValueAvailable && (vol != vol_p)) { 
        // If capture event AND volume value changed (saves runtime resources)
        vol_p = vol;
        vol = max(vol, 5000);

        vol_v = vol; // Averaging volume values
        vol_v = vol_l + ((vol_v - vol_l) >> 2);
        vol_l = vol_v;

        if (g_audio_output_is_enabled) {
            vol_v = MAX_VOLUME - (volCalibrationBase - vol_v) / 2 + (volumePotValue << 2) - 1024;
            // Limit and set volume value
            vol_v = min(vol_v, 4095);
            vol_v = max(vol_v, 0);
            tmpVolume = vol_v >> 4;
            // Give vScaledVolume a pseudo-exponential characteristic for the final audio output
            vScaledVolume = tmpVolume * (tmpVolume + 2);
        } else {
            vScaledVolume = 0;
        }

#if CV_OUTPUT_MODE != CV_OUTPUT_MODE_OFF
        static bool gate_p = false;
        tmpVolume = tmpVolume >> 1;
        // Most synthesizers "exponentiate" the volume CV themselves
        // so we send the "raw" volume for CV
        volCV = vol_v;
        volumeCVAvailable = true;

        // Drive the GATE output pin if CV is activated in "build.h"
        if (!gate_p && (tmpVolume >= GATE_ON_THRESHOLD_LEVEL)) {
            gate_p = true;
            // pull the gate up to sense, first (to prevent short-circuiting the IO pin:
            GATE_PULLUP;
            if (GATE_SENSE) { 
                // if it goes up, drive the gate full high:
                GATE_DRIVE_HIGH;
            }
        } else if (gate_p && (tmpVolume <= GATE_OFF_THRESHOLD_LEVEL)) {
            gate_p = false;
            // drive the gate low:
            GATE_DRIVE_LOW;
        }
#endif // GATE IF ENABLED
        volumeValueAvailable = false;
    }

    goto mloop; // End of main loop
}