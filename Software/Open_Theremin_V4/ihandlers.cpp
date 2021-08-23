#include "Arduino.h"

#include "ihandlers.h"
#include "SPImcpDAC.h"
#include "timer.h"
#include "build.h"

#include "theremin_sintable.c"
#include "theremin_sintable2.c"
#include "theremin_sintable3.c"
#include "theremin_sintable4.c"
#include "theremin_sintable5.c"
#include "theremin_sintable6.c"
#include "theremin_sintable7.c"
#include "theremin_sintable8.c"

const int16_t* const wavetables[] = { //Fixed following a suggestion by Michael Freitas, does not need to be in PROGMEM
  sine_table,
  sine_table2,
  sine_table3,
  sine_table4,
  sine_table5,
  sine_table6,
  sine_table7,
  sine_table8
};

static const uint32_t MCP_DAC_BASE = 2048;

#define INT0_STATE    (PIND & (1<<PORTD2))
#define PC_STATE      (PINB & (1<<PORTB0))

// Added by ThF 20200419
// #define TH_DEBUG 			// <-- comment this out for normal operation
// end

#ifdef TH_DEBUG
#include "hw.h"
#endif

volatile uint16_t vScaledVolume = 0;
volatile uint16_t vPointerIncrement = 0;

volatile uint16_t pitch = 0;            // Pitch value
volatile uint16_t pitch_counter = 0;    // Pitch counter
volatile uint16_t pitch_counter_l = 0;  // Last value of pitch counter

volatile bool volumeValueAvailable = 0;  // Volume read flag
volatile bool pitchValueAvailable = 0;   // Pitch read flag
volatile bool reenableInt1 = 0;   // reeanble Int1

volatile uint16_t vol;                   // Volume value
volatile uint16_t vol_counter = 0;
volatile uint16_t vol_counter_i = 0;     // Volume counter
volatile uint16_t vol_counter_l;         // Last value of volume counter

volatile uint16_t timer_overflow_counter;         // counter for frequency measurement

volatile uint8_t vWavetableSelector = 0;  // wavetable selector

static volatile uint16_t pointer       = 0;  // Table pointer
static volatile uint8_t  debounce_p, debounce_v  = 0;  // Counters for debouncing

void ihInitialiseTimer() {
  /* Setup Timer 1, 16 bit timer used to measure pitch and volume frequency */
  TCCR1A = 0;                     // Set Timer 1 to Normal port operation (Arduino does activate something here ?)
  TCCR1B = (1<<ICES1)|(1<<CS10);  // Input Capture Positive edge select, Run without prescaling (16 Mhz)
  TIMSK1 = (1<<ICIE1);            // Enable Input Capture Interrupt
  
  TCCR0A = 3; //Arduino Default: Fast PWM
  TCCR0B = 3; //Arduino Default: clk I/O /64 (From prescaler)
  TIMSK0 = 1; //Arduino Default: TOIE0: Timer/Counter0 Overflow Interrupt Enable
  
}

void ihInitialiseInterrupts() {
  /* Setup interrupts for Wave Generator and Volume read */
  EICRA = (1<<ISC00)|(1<<ISC01)|(1<<ISC11)|(1<<ISC10) ; // The rising edges of INT0 and INT1 generate an interrupt request.
  reenableInt1 = true;
  EIMSK = (1<<INT0)|(1<<INT1);                          // Enable External Interrupt INT0 and INT1
}

void ihInitialisePitchMeasurement() //Measurement of variable frequency oscillator on Timer 1
{   reenableInt1 = false;
    EIMSK =  0; // Disable External Interrupts
    TCCR1A = 0;           //Normal port operation Timer 1
    TIMSK1 = (1<<TOIE1);  //Timer/Counter1, Overflow Interrupt Enable
   
  }
  
void ihInitialiseVolumeMeasurement() //Measurement of variable frequency oscillator on Timer 0
{   reenableInt1 = false;
    EIMSK =  0; // Disable External Interrupts
    TIMSK1 = 0; //Timer/Counter1, Overflow Interrupt Disable

    TCCR0A = 0; // Normal port operation, OC0A disconnected. Timer 0
    TIMSK0 = (1<<OCIE0A);  //TOIE0: Timer/Counter0 Overflow Interrupt Enable
    OCR0A = 0xff; // set Output Compare Register0.
    
    TCCR1A = 0;  //Normal port operation Timer 1
    TCCR1B = (1<<CS10)|(1<<CS12); // clk I/O /1024 (From prescaler)
    TCCR1C=0;

    
  }

/* Externaly generated 31250 Hz Interrupt for WAVE generator (32us) */
ISR (INT1_vect) {
  // Interrupt takes up normally 14us but can take up to 22us when interrupted by another interrupt.

  // Added by ThF 20200419
  #ifdef TH_DEBUG
    HW_LED2_ON;
  #endif

  // Latch previously written DAC value:
  SPImcpDAClatch();

	disableInt1(); // Disable External Interrupt INT1 to avoid recursive interrupts
	// Enable Interrupts to allow counter 1 interrupts
	interrupts();

	int16_t waveSample;
	uint32_t scaledSample = 0;
	uint16_t offset = (uint16_t)(pointer >> 6) & 0x3ff;

#if CV_ENABLED                                 // Generator for CV output

 vPointerIncrement = min(vPointerIncrement, 4095);
 SPImcpDACsend(vPointerIncrement);        //Send result to Digital to Analogue Converter (audio out) (5.5 us)

#else   //Play sound
 SPImcpDAC3Asend(vPointerIncrement);        //Send result to Digital to Analogue Converter (audio out) (5.5 us)
 SPImcpDAC3Bsend(vScaledVolume>> 5);        //Send result to Digital to Analogue Converter (audio out) (5.5 us)

	// Read next wave table value
	waveSample = (int16_t)pgm_read_word_near(wavetables[vWavetableSelector] + offset);

	scaledSample = ((int32_t)waveSample * (uint32_t)vScaledVolume) >> 16; // The compiler optimizes this better than any assembly written by hand !!!

	SPImcpDACsend(scaledSample + MCP_DAC_BASE); //Send result to Digital to Analogue Converter (audio out) (5.5 us)

	pointer += vPointerIncrement; // increment table pointer

#endif                          //CV play sound
  incrementTimer();               // update 32us timer

  if (PC_STATE) debounce_p++;
  if (debounce_p == 3) {
    noInterrupts();
    pitch_counter = ICR1;                      // Get Timer-Counter 1 value
    pitch = (pitch_counter - pitch_counter_l); // Counter change since last interrupt -> pitch value
    pitch_counter_l = pitch_counter;           // Set actual value as new last value
  };

  if (debounce_p == 5) {
    pitchValueAvailable = true;
  };

  if (INT0_STATE) debounce_v++;
  if (debounce_v == 3) {
    noInterrupts();
    vol_counter = vol_counter_i;            // Get Timer-Counter 1 value
    vol = (vol_counter - vol_counter_l);    // Counter change since last interrupt
    vol_counter_l = vol_counter;            // Set actual value as new last value
  };

  if (debounce_v == 5) {
    volumeValueAvailable = true;
  };

  noInterrupts();
  enableInt1();
// Added by ThF 20200419
#ifdef TH_DEBUG
	HW_LED2_OFF;
#endif
}

/* VOLUME read - interrupt service routine for capturing volume counter value */
ISR (INT0_vect) {
  vol_counter_i = TCNT1;
  debounce_v = 0;
};


/* PITCH read - interrupt service routine for capturing pitch counter value */
ISR (TIMER1_CAPT_vect) {
  debounce_p = 0;
};


/* PITCH read absolute frequency - interrupt service routine for calibration measurement */
ISR(TIMER0_COMPA_vect)
{
  timer_overflow_counter++;
  }

/* VOLUME read absolute frequency - interrupt service routine for calibration measurement */
ISR(TIMER1_OVF_vect)
{
  timer_overflow_counter++;
}
