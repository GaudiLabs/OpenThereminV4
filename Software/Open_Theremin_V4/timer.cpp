#include "Arduino.h"

#include "timer.h"

volatile uint16_t timer = 0;

void ticktimer (uint16_t ticks) {
  resetTimer();
  while (timerUnexpired(ticks))
    ;  // NOP
};

void millitimer (uint16_t milliseconds) {
  ticktimer(millisToTicks(milliseconds));
}
