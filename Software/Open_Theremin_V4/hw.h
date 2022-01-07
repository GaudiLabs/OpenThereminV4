
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

#define GATE_PULLUP         (DDRC &= ~(1<<PORTC2)); (PORTC |= (1<<PORTC2))
#define GATE_SENSE          (PINC & (1<<PORTC2))
#define GATE_DRIVE_HIGH     (DDRC |= (1<<PORTC2))
#define GATE_DRIVE_LOW      (PORTC &= ~(1<<PORTC2)); (DDRC |= (1<<PORTC2))

#endif // _HW_H
