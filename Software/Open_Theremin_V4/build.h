// Build definitions

#ifndef _BUILD_H
#define _BUILD_H


// Set to build with serial support
#define SERIAL_ENABLED 0

// Set to build with logarithmic 1V/oct pitch control voltage output
#define CV_LOG 1

// Set the trigger levels for the Gate signal (0 to 127 in preparation for later midi extensions)
#define GATE_ON  20 // That's the level which will drive the Gate high when volume increases from lower
#define GATE_OFF 16 // That's the level which will drive the Gate low when volume decreases from higher
// Making both values equal risk the gate signal to bounce, leave at least 4 (hysteresis) between both.


#endif // _BUILD_H
