#ifndef _IHANDLERS_H
#define _IHANDLERS_H

extern volatile uint16_t pitch;              // Pitch value
extern volatile uint16_t vol;                // Volume value
extern volatile uint16_t vScaledVolume;      // Volume byte
extern volatile int16_t pitchCV;              // Pitch CV value
extern volatile uint16_t volCV;                // Volume CV value

extern volatile uint16_t pitch_counter;      // Pitch counter
extern volatile uint16_t pitch_counter_l;    // Last value of pitch counter

extern volatile uint16_t vol_counter;      // Pitch counter
extern volatile uint16_t vol_counter_l;    // Last value of pitch counter

extern volatile uint16_t timer_overflow_counter;         // counter for frequency measurement


extern volatile bool volumeValueAvailable;   // Volume read flag
extern volatile bool pitchValueAvailable;    // Pitch read flag
extern volatile bool volumeCVAvailable;   // Volume CV flag
extern volatile bool pitchCVAvailable;    // Pitch CV flag
extern volatile bool reenableInt1;    // Pitch read flag

extern volatile uint8_t  vWavetableSelector;
extern volatile uint16_t vPointerIncrement; // Table pointer increment

inline void resetPitchFlag()   { pitchValueAvailable = false; }
inline void resetVolFlag()     { volumeValueAvailable = false; }

inline void savePitchCounter() { pitch_counter_l=pitch_counter; }
inline void saveVolCounter()   { vol_counter_l=vol_counter; };

inline void setWavetableSampleAdvance(uint16_t val) { vPointerIncrement = val;}

inline void disableInt1() { EIMSK &= ~ (1 << INT1); }
inline void enableInt1()  { if (reenableInt1) EIMSK |=   (1 << INT1); }

void ihInitialiseTimer();
void ihInitialiseInterrupts();
void ihInitialisePitchMeasurement();
void ihInitialiseVolumeMeasurement();
void resetPitchFlag();
void resetVolFlag();
void savePitchCounter();
void saveVolCounter();

#endif // _IHANDLERS_H
