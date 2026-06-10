#include "calibration.h"
#include "SPImcpDAC.h"
#include "timer.h"
#include "ihandlers.h"
#include "EEPROM.h"
#include "build.h"
#include "hw.h"


// calibration
#define DAC_12BIT_MAX 4095         // 12 bit DAC max clamping value
#define PITCH_FIXED_OSCILLATOR_FREQUENCY        500000      // XTAL1 = 16 MHz / 32 (Q5 of U2 - 4060D)
#define VOLUME_FIXED_OSCILLATOR_FREQUENCY       460800      // XTAL2 = 7.3728 MHz / 16 (Q4 of U3 - 4060D)
#define CALIBRATION_BASE_MAX_DRIFT              150         // maximum drift from target frequency for success calibration
const int16_t CalibrationTolerance = 15;
int32_t pitchCalibrationBase = 0;
const int16_t PitchFreqOffset = 700;
int32_t volCalibrationBase = 0;
const int16_t VolumeFreqOffset = 700;
static float qMeasurement = 0;


/*******************************************
 *          CALIBRATION ROUTINES
 *******************************************/

unsigned long GetQMeasurement() {
  int qn = 0;
  TCCR1B = (1 << CS10);
  while (!(PIND & (1 << PORTD3)));
  while ((PIND & (1 << PORTD3)));
  TCNT1 = 0;
  timer_overflow_counter = 0;
  while (qn < 31250) {
    while (!(PIND & (1 << PORTD3)));
    qn++;
    while ((PIND & (1 << PORTD3)));
  }
  TCCR1B = 0;
  unsigned long frequency = TCNT1;
  unsigned long temp = 65536 * (unsigned long)timer_overflow_counter;
  frequency += temp;
  return frequency;
}


 
/**
 * @brief Measures the absolute frequency of the pitch oscillator (VO_PITCH).
 *
 * This function uses Timer1 in normal mode to count the number of clock cycles
 * over a 1-second window (via `delay(1000)`) to compute the frequency of the
 * signal arriving on PD5 (ICP1).
 *
 * Timer1 is configured with a prescaler of 1024, so its effective counting frequency is:
 *   16,000,000 / 1024 = 15,625 Hz.
 *
 * The function resets the counter and overflow register before starting measurement,
 * then accumulates the total number of clock ticks over the 1-second duration.
 *
 * @details
 * - This method is typically called during the calibration procedure.
 * - It indirectly measures VO_PITCH, a ~500 kHz RF signal, by leveraging
 *   frequency aliasing from the physical RF circuitry and flip-flop gating (F_PITCH).
 * - Timer1 overflows at 65536 ticks, so the `timer_overflow_counter` accounts for overflows.
 *
 * The final frequency count is:
 * @code
 *   frequency = TCNT1 + 65536 * timer_overflow_counter;
 * @endcode
 *
 *
 * @return The measured pitch frequency in Timer1 ticks over 1 second, to be converted externally.
 */
uint32_t GetPitchMeasurement() {
    DEBUG_PRINT(F("GetPitchMeasurement "));
    TCNT1 = 0;                      // Reset Timer1 counter
    timer_overflow_counter = 0;     // Reset overflow counter
    // Set Timer1 with prescaler 1024 (CS12 | CS11 | CS10)
    // Resulting timer tick: 16 MHz / 1024 = 15.625 kHz
    TCCR1B = (1 << CS12) | (1 << CS11) | (1 << CS10);
    delay(1000);                    // Measure duration: 1000 ms (blocking)
    TCCR1B = 0;                     // Stop Timer1 after measurement
    // Combine Timer1 count with overflow count to get full frequency count
    uint32_t frequency = TCNT1;
    uint32_t temp = 65536UL * (uint32_t)timer_overflow_counter;
    frequency += temp;
    // measured_Hz = measuredTicks * (16e6 / 1024) / 1.0 for 1s integration time
    DEBUG_PRINT(F("tcnt1="));DEBUG_PRINT(TCNT1);DEBUG_PRINT(F(" ovfl="));DEBUG_PRINT(timer_overflow_counter);DEBUG_PRINT(F(" f="));DEBUG_PRINTLN(frequency);
    return frequency;
}

/**
 * @brief Measures the frequency of the volume antenna oscillator (VO_VOL).
 *
 * This function uses Timer/Counter0 in external clock mode to count rising edges
 * from the VO_VOL signal (connected to T0 / PD4 / Digital Pin 4) over a 1-second
 * gate window determined by Timer1 overflow.
 *
 * Timer1 is preloaded with 49911 to overflow after ~1 second (65536 - 49911 = 15625 ticks @ 16 MHz).
 * Timer0 counts external rising edges from VO_VOL during this gate time.
 * The result is the total number of VO_VOL cycles within one second.
 *
 * @return Frequency in Hz as an uint32_t value.
 *
 * @note Timer0 is 8-bit, so an overflow counter is used to extend its range.
 *       This approach trades resolution for simplicity, which is acceptable for volume
 *       since it does not require the same precision or update rate as pitch.
 *
 * @warning This method assumes Timer0 and Timer1 are not used elsewhere during execution.
 */
uint32_t GetVolumeMeasurement() {
    DEBUG_PRINT(F("GetVolumeMeasurement "));
    timer_overflow_counter = 0;                                 // Clear overflow tracker for Timer0
    TCNT0 = 0;                                                  // Reset Timer0 (pulse counter)
    TCNT1 = 49911;                                              // Preload Timer1 so it overflows in 1s (65536 - 49911 = 15625 ticks @ 16 MHz)
    // Set Timer0 to external clock on T0 (PD4), count on rising edge
    TCCR0B = (1 << CS02) | (1 << CS01) | (1 << CS00);
    // Clear Timer1 overflow flag (to detect overflow event)
    TIFR1 = (1 << TOV1);                                        //Timer1 INT Flag Reg: Clear Timer Overflow Flag
    while (!(TIFR1 & ((1 << TOV1)))) {}                         // Wait until Timer1 overflows (1s interval)
    TCCR0B = 0;                                                 // Stop TimerCounter 0
    uint32_t frequency = TCNT0;                                 // Retrieve measured pulse count from Timer0 and overflow counter
    uint32_t temp = (uint32_t)timer_overflow_counter;           // Each Timer0 overflow = 256 pulses
    frequency += temp * 256;
    DEBUG_PRINT(F("tcnt0="));DEBUG_PRINT(TCNT0);DEBUG_PRINT(F(" ovfl="));DEBUG_PRINT(temp);DEBUG_PRINT(F(" f="));DEBUG_PRINTLN(frequency);
    return frequency;
}

/**
 * @brief Completes the automatic calibration routine for pitch and volume oscillators.
 *
 * This function captures reference frequency measurements from both VO_PITCH and VO_VOL,
 * storing baseline counter values and computing calibration constants used during runtime
 * for frequency-to-pitch/volume translation. The results are saved into EEPROM for persistence.
 *
 * ### Calibration Steps:
 * 1. **Pitch:**
 *    - Resets internal pitch flags and timers.
 *    - Waits for a valid `pitchValueAvailable` flag, or times out after 10 ms.
 *    - Captures the base pitch period and computes:
 *      - `pitchCalibrationBase`: raw Timer1 delta
 *
 * 2. **Volume:**
 *    - Same logic as pitch, using `volValueAvailable` and `vol_counter`.
 *    - Captures `volCalibrationBase` from the Timer1 delta.
 *
 * 3. **EEPROM Storage:**
 *    - Stores calibration values:
 *      - At address 4: `pitchCalibrationBase` (4 bytes)
 *      - At address 8: `volCalibrationBase` (4 bytes)
 *
 * @returns true if success, to avoid writing invalid calibration data in EEPROM
 *
 * @note Calibration uses blocking wait with optional timeout of 10 ms for each channel.
 *       It assumes the player is holding still, and RF conditions are stable.
 *
 * @warning Requires successful pitch/volume ISR processing to populate
 *          `pitchValueAvailable` and `volumeValueAvailable` before timeout.
 *
 * @see GetPitchMeasurement()
 * @see GetVolumeMeasurement()
 */
bool calibration_finalize() {
    // use temporary storage in case calibration fails
    // so to store only in case both have success
    // avoid partial calibration changes
    int32_t pitch_calibration_val = 0;
    int32_t volume_calibration_val = 0;
    bool success = false;

    // --- Final Pitch calibration persistance ---
    pitchValueAvailable = false;   //< Clear pitchValueAvailable
    resetTimer();       // Reset system timer
    pitch_counter_l = pitch_counter; // Store previous pitch counter value

    // Wait for new pitch value or timeout after 10ms
    while (!success && !timerExpiredMillis(10)) { success = pitchValueAvailable; }
    if (success) {
        success = false; // retry for volume
        pitch_calibration_val = pitch;           // Store raw pitch counter
        // --- Final Volume calibration persistance---
        volumeValueAvailable = false;     // Clear volumeValueAvailable
        resetTimer();       // Reset system timer
        vol_counter_l = vol_counter;   // Store previous volume counter value
        // Wait for new volume value or timeout after 10ms
        while (!success && !timerExpiredMillis(10)) { success = volumeValueAvailable; }
        if (success) {
            volume_calibration_val = vol;               // Store raw volume counter
            #ifdef SERIAL_DEBUG_MESSAGES
            printCalibrationDetails();
            #endif

            // check if calibration was within valid limits
            float pitchBeatHz = qMeasurement / pitchCalibrationBase;
            float volBeatHz = qMeasurement / volCalibrationBase;
            success = abs(pitchBeatHz - PitchFreqOffset) < CALIBRATION_BASE_MAX_DRIFT;
            if (success) {
                success = abs(volBeatHz - VolumeFreqOffset) < CALIBRATION_BASE_MAX_DRIFT;
                if (success) {
                    pitchCalibrationBase = pitch_calibration_val;
                    volCalibrationBase = volume_calibration_val;
                    // --- Store calibration baseline results in EEPROM ---
                    EEPROM.put(EEPROM_PITCH_DAC_CALIBRATION_BASE_ADDRESS, pitchCalibrationBase);    // Save pitch baseline (4 bytes)
                    EEPROM.put(EEPROM_VOLUME_DAC_CALIBRATION_BASE_ADDRESS, volCalibrationBase);     // Save volume baseline (4 bytes)
                }
            }
        }
    }
    return success;
}

/**
 * @brief Performs pitch oscillator calibration using a numerical approximation.
 *
 * This function tunes the DAC output controlling the VO_PITCH oscillator to align
 * the oscillator frequency with the reference frequency derived from the 16 MHz crystal.
 *
 * ### Calibration Goal:
 * - Find the correct DAC setting (`pitchXn`) so that the pitch oscillator outputs exactly 500 kHz,
 *   matching the divided reference frequency (U2 Q5 = 16 MHz / 32).
 *
 * ### Algorithm Summary:
 * - The search interval is initialized with `pitchXn0 = 0` and `pitchXn1 = 4095`.
 * - It uses a form of secant method (similar to dichotomy/bisection) to iteratively find
 *   the optimal DAC value that minimizes the frequency delta (`pitchfn0 - pitchfn1`).
 * - Loop is limited to 12 iterations, sufficient for 12-bit resolution convergence.
 *
 * ### EEPROM Storage:
 * - The final calibrated DAC value is written to EEPROM address 0.
 * 
 * @returns true if successfully calibrated.
 *
 * @warning Requires VO_PITCH oscillator to be stable during calibration.
 * @note Uses blocking `delay()` and relies on GetPitchMeasurement(), which uses Timer1 counting.
 * @see GetQMeasurement()
 * @see GetPitchMeasurement()
 */
bool calibrate_pitch() {
    int16_t pitchXn0 = 0;
    int16_t pitchXn1 = DAC_12BIT_MAX;
    int16_t pitchXn2 = 0;
    float q0 = 0.f;
    long pitchfn0 = 0;
    long pitchfn1 = 0;
    long pitchfn = 0;

    DEBUG_PRINTLN(F("\nPITCH CALIBRATION"));
    DEBUG_PRINT(F("Set f= ")); DEBUG_PRINTLN(pitchfn);

    // UI FEEDBACK
    HW_LED_BLUE_ON;
    HW_LED_RED_ON;

    ihInitialisePitchMeasurement();     // Setup Timer1 and associated ISR flags
    sei();
    SPImcpDACinit();                    // Initialize DACs

    qMeasurement = GetQMeasurement(); // Measure Arduino clock frequency
    DEBUG_PRINT(F("X1="));DEBUG_PRINTLN(qMeasurement);
    q0 = (16000000 / qMeasurement * PITCH_FIXED_OSCILLATOR_FREQUENCY); //Calculated set frequency based on Arudino clock frequency

    pitchfn = q0 - PitchFreqOffset; // Add offset calue to set frequency
    DEBUG_PRINT(F("Pitch Set frequency: "));DEBUG_PRINTLN(pitchfn);

    SPImcpDAC2Bsend(1600);          // bias the volume oscillator for pitch calibration
    SPImcpDAC2Asend(pitchXn0);      // Frequency at low DAC value
    delay(100);
    pitchfn0 = GetPitchMeasurement();
    SPImcpDAC2Asend(pitchXn1);      // Frequency at high DAC value
    delay(100);
    pitchfn1 = GetPitchMeasurement();

    DEBUG_PRINT(F("Tune range ")); DEBUG_PRINT(pitchfn0); DEBUG_PRINT(F(" to ")); DEBUG_PRINTLN(pitchfn1);

    uint8_t cal_iteration = 0; // Max 12 iterations
    while (abs(pitchfn0 - pitchfn1) > CalibrationTolerance && (cal_iteration < 12)) {
        SPImcpDAC2Asend(pitchXn0);
        delay(100);
        pitchfn0 = GetPitchMeasurement() - pitchfn;

        SPImcpDAC2Asend(pitchXn1);
        delay(100);
        pitchfn1 = GetPitchMeasurement() - pitchfn;

        // Secant approximation to refine DAC value
        pitchXn2 = pitchXn1 - ((pitchXn1 - pitchXn0) * pitchfn1) / (pitchfn1 - pitchfn0); // new DAC value

        DEBUG_PRINT("\nDAC L: "); DEBUG_PRINT(pitchXn0); DEBUG_PRINT(" fL: "); DEBUG_PRINTLN(pitchfn0);
        DEBUG_PRINT("DAC H: "); DEBUG_PRINT(pitchXn1); DEBUG_PRINT(" fH: "); DEBUG_PRINTLN(pitchfn1);

        pitchXn0 = pitchXn1;
        pitchXn1 = pitchXn2;

        HW_LED_BLUE_TOGGLE;     // visual feedback
        cal_iteration++;
    }
    delay(100);

    bool success = cal_iteration < 12;
    if (success) {
        EEPROM.put(EEPROM_PITCH_DAC_VOLTAGE_ADDRESS, pitchXn0); // Store final calibrated DAC value for VO_PITCH
    }
    return success;
}

/**
 * @brief Replacement function for delay() used only during VO_VOL measurement in calibration.
 *
 * calling delay() will block the MCU completely because the same timer
 * used by delay() is already being used for the actual frequency measurement.
 * The fixed constant 44316 represents the equivalent of 100ms of delay.
 *
 * @warning Requires stable oscillator and clean signal edges on VO_VOL for accurate timing.
 * 
 */
void delay_NOP() {
    volatile unsigned long i = 0;
    for (i = 0; i < 44316; i++) {
        __asm__ __volatile__("nop");
    }
}


/**
 * @brief Performs volume oscillator calibration using iterative frequency measurement.
 *
 * This function finds the optimal DAC value that aligns the VO_VOL oscillator frequency
 * with the reference frequency derived from the second crystal (X2 at 7.3728 MHz).
 *
 * ### Calibration Goal:
 * - Align the VO_VOL oscillator frequency with a nominal target:  
 *   7.3728 MHz / 16 (Q4 from U2) = 460,800 Hz.
 *
 * ### Calibration Strategy:
 * - Like the pitch routine, this uses a secant-like numerical approach over 12-bit DAC range.
 * - Uses a 100 ms delay (`waitNOP(44316)`) between DAC value changes and frequency measurement
 *   to ensure the RF circuit stabilizes.
 *
 * ### EEPROM Storage:
 * - The resulting DAC value is stored at EEPROM address 2.
 *
 * @returns true if successfully calibrated.
 * 
 * @note The DAC2B channel is used for volume oscillator control.
 * @warning Requires stable oscillator and clean signal edges on VO_VOL for accurate timing.
 * @see GetVolumeMeasurement()
 */
bool calibrate_volume() {
    int16_t volumeXn0 = 0;   // Lower DAC bound
    int16_t volumeXn1 = DAC_12BIT_MAX;   // Upper DAC bound
    int16_t volumeXn2 = 0;   // Next DAC trial value

    int32_t volumefn0 = 0;      // Frequency measurements
    int32_t volumefn1 = 0;

    DEBUG_PRINTLN(F("\nVOLUME CALIBRATION"));

    ihInitialiseVolumeMeasurement();    // Configure Timer0 and related registers
    sei();
    SPImcpDACinit();                    // Re-initialize DAC interface

    // Target frequency: 7372800 / 16 = 460800
    float q0 = (16000000  / qMeasurement  * VOLUME_FIXED_OSCILLATOR_FREQUENCY);            // Calculated oscillator frequency based on crystal measurement
    int32_t volumefn = q0 - VolumeFreqOffset;
    DEBUG_PRINT(F("Vf=")); DEBUG_PRINTLN(volumefn);

    DEBUG_PRINT(F("Volume Set Frequency: "));DEBUG_PRINTLN(volumefn);

    // Measure low and high frequency extremes for DAC sweep range
    SPImcpDAC2Bsend(volumeXn0);
    delay_NOP(); 
    volumefn0 = GetVolumeMeasurement();
    SPImcpDAC2Bsend(volumeXn1);
    delay_NOP();
    volumefn1 = GetVolumeMeasurement();

    DEBUG_PRINT(F("Tune range ")); DEBUG_PRINT(volumefn0); DEBUG_PRINT(" to "); DEBUG_PRINTLN(volumefn1);

    uint8_t cal_iteration = 0; // Max 12 iterations
    while (abs(volumefn0 - volumefn1) > CalibrationTolerance && (cal_iteration < 12)) {
        SPImcpDAC2Bsend(volumeXn0);
        delay_NOP();
        volumefn0 = GetVolumeMeasurement() - volumefn;
        SPImcpDAC2Bsend(volumeXn1);
        delay_NOP();
        volumefn1 = GetVolumeMeasurement() - volumefn;
        // Secant method refinement
        volumeXn2 = volumeXn1 - ((volumeXn1 - volumeXn0) * volumefn1) / (volumefn1 - volumefn0); // calculate new DAC value

        DEBUG_PRINT("\nDAC L: "); DEBUG_PRINT(volumeXn0); DEBUG_PRINT(" fL: "); DEBUG_PRINTLN(volumefn0);
        DEBUG_PRINT("DAC H: "); DEBUG_PRINT(volumeXn1); DEBUG_PRINT(" fH: "); DEBUG_PRINTLN(volumefn1);

        volumeXn0 = volumeXn1;
        volumeXn1 = volumeXn2;

        HW_LED_BLUE_TOGGLE;     // visual feedback
        cal_iteration++;
    }
    bool success = cal_iteration < 12;
    if (success) {
        EEPROM.put(EEPROM_VOLUME_DAC_VOLTAGE_ADDRESS, volumeXn0); // Store DAC value for volume oscillator compensation
    }

    HW_LED_BLUE_ON;
    HW_LED_RED_OFF;
    return success;
}

bool calibration_start() {    
    bool success = calibrate_pitch();
    if (success) {
        success = calibrate_volume();
    } else {
        // restore previous calibration DAC values if failed
        calibration_read();
    }

    ihInitialiseTimer();
    ihInitialiseInterrupts();
    millitimer(100);
    if (success) {
        success = calibration_finalize();
        DEBUG_PRINTLN(F("CAL OK"));
    } else {
        DEBUG_PRINTLN(F("CAL ERR"));
    }
    return success;
}


void calibration_read() {
    SPImcpDACinit();
    int16_t pitchDAC = 0;
    int16_t volumeDAC = 0;
    EEPROM.get(EEPROM_PITCH_DAC_VOLTAGE_ADDRESS, pitchDAC);
    EEPROM.get(EEPROM_VOLUME_DAC_VOLTAGE_ADDRESS, volumeDAC);
    EEPROM.get(EEPROM_PITCH_DAC_CALIBRATION_BASE_ADDRESS, pitchCalibrationBase);
    EEPROM.get(EEPROM_VOLUME_DAC_CALIBRATION_BASE_ADDRESS, volCalibrationBase);
    SPImcpDAC2Asend(pitchDAC);
    SPImcpDAC2Bsend(volumeDAC);
    
    #ifdef SERIAL_DEBUG_MESSAGES
    printCalibrationDetails();
    #endif

    // TODO: check if valid calibration
}