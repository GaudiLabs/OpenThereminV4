#ifndef _HW_H
#define _HW_H

#define HW_BUTTON_STATE    (PIND & (1<<PORTD6))
#define HW_BUTTON_PRESSED  (HW_BUTTON_STATE == LOW)
#define HW_BUTTON_RELEASED (HW_BUTTON_STATE != LOW)

#define HW_LED1_ON          (PORTC |= (1<<PORTC4))
#define HW_LED1_OFF         (PORTC &= ~(1<<PORTC4))

#define HW_LED2_ON          (PORTC |= (1<<PORTC5))
#define HW_LED2_OFF         (PORTC &= ~(1<<PORTC5))

#define HW_LED1_TOGGLE      (PORTC = PORTC ^ (1<<PORTC4))
#define HW_LED2_TOGGLE      (PORTC = PORTC ^ (1<<PORTC5))

#endif // _HW_H
