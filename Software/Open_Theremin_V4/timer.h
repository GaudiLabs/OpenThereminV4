#ifndef _TIMER_H
#define _TIMER_H

extern volatile uint16_t timer;

inline uint16_t millisToTicks(uint16_t milliseconds) {
  return milliseconds * (1000.0f/32);
}

inline void resetTimer() {
  timer = 0;
}

inline void incrementTimer() {
  timer++;
}

inline bool timerExpired(uint16_t ticks) {
  return timer >= ticks;
}

inline bool timerUnexpired(uint16_t ticks) {
  return timer < ticks;
}

inline bool timerExpiredMillis(uint16_t milliseconds) {
  return timerExpired(millisToTicks(milliseconds));
}

inline bool timerUnexpiredMillis(uint16_t milliseconds) {
  return timerUnexpired(millisToTicks(milliseconds));
}

void ticktimer (uint16_t ticks);
void millitimer (uint16_t milliseconds);

#if SERIAL_ENABLED
const uint16_t TICKS_100_MILLIS = millisToTicks(100);
#endif //SERIAL_ENABLED

#endif // _TIMER_H
