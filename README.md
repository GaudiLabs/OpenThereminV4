# OpenThereminV4
Open Source Theremin Instrument

Schematics, printed circuit board (PCB) design, bill of materials (BOM) and Arduino UNO compatible software for the OpenTheremin V4

### Open Source Theremin based on the Arduino Platform

OpenTheremin is an arduino based real Therein instrument. The legendary music instrument was invented by Leon Theremin back in 1920. The theremin is played with two antennas, one to control the pitch and one for volume. The electronic shield with two ports to connect those antennas comprises two heterodyne oscillators to measure the distance of the hand to the antenna when playing the instrument. The resulting signal is fed into a AtMega328P microcontroller. After linearization and filtering the circuit generates the instruments sound that is then played through a high quality digital analog audio converter on the board. The characteristics of the sound can be determined by a wave table on the arduino.

For more info on the open source project and on availability of ready made shield see:

http://www.gaudi.ch/OpenTheremin/

### Installation
1. Open up the Arduino IDE
2. Open the File "Open_Theremin_V4.ino"
3. Selecting the correct usb port on Tools -> Serial Port
4. Select the correct arduino board from Tools -> Board
5. Upload the code by clicking on the upload button.

