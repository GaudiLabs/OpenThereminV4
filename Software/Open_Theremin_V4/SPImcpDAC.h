/* Control the mcp 4921/4922 DACs with hardware SPI of the Arduino UNO
 * ...without all the overhead of the Arduino SPI lib...
 * Just the needed functions in a runtime optimized way by "Theremingenieur" Thierry Frenkel
 * This file is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SPImcpDac_h
#define SPImcpDac_h

#include <Arduino.h>

// Data direction & Port register & Bit number for DAC Latch:
#define MCP_DAC_LDAC_DDR DDRD
#define MCP_DAC_LDAC_PORT PORTD
#define MCP_DAC_LDAC_BIT 7
// Data direction & Port register & Bit number for DAC CS
#define MCP_DAC_CS_DDR DDRB
#define MCP_DAC_CS_PORT PORTB
#define MCP_DAC_CS_BIT 2
// Data direction & Port register & Bit number for DAC2 CS
#define MCP_DAC2_CS_DDR DDRB
#define MCP_DAC2_CS_PORT PORTB
#define MCP_DAC2_CS_BIT 1
// Data direction & Port register & Bit number for DAC3 CS
#define MCP_DAC3_CS_DDR DDRC
#define MCP_DAC3_CS_PORT PORTC
#define MCP_DAC3_CS_BIT 3
// Data direction & Port registers & Bit numbers for Hardware SPI
#define HW_SPI_DDR DDRB
#define HW_SPI_SCK_BIT 5
#define HW_SPI_MISO_BIT 4 // unused in this configuration
#define HW_SPI_MOSI_BIT 3

static inline void SPImcpDACinit()
{
	// initialize the latch pin:
	MCP_DAC_LDAC_DDR |= _BV(MCP_DAC_LDAC_BIT);
	MCP_DAC_LDAC_PORT |= _BV(MCP_DAC_LDAC_BIT);
	// initialize the CS pins:
	MCP_DAC_CS_DDR   |= _BV(MCP_DAC_CS_BIT);
	MCP_DAC_CS_PORT  |= _BV(MCP_DAC_CS_BIT);
	MCP_DAC2_CS_DDR  |= _BV(MCP_DAC2_CS_BIT);
	MCP_DAC2_CS_PORT |= _BV(MCP_DAC2_CS_BIT);
  MCP_DAC3_CS_DDR  |= _BV(MCP_DAC3_CS_BIT);
  MCP_DAC3_CS_PORT |= _BV(MCP_DAC3_CS_BIT);
	// initialize the hardware SPI pins:
	HW_SPI_DDR |= _BV(HW_SPI_SCK_BIT);
	HW_SPI_DDR |= _BV(HW_SPI_MOSI_BIT);
	// initialize the hardware SPI registers
	SPCR = _BV(SPE) | _BV(MSTR);	// no interrupt, SPI enable, MSB first, SPI master, SPI mode 0, clock = f_osc/4 (maximum)
	SPSR = _BV(SPI2X);  // double the SPI clock, ideally we get 8 MHz, so that a 16bit word goes out in 3.5us (5.6us when called from an interrupt) including CS asserting/deasserting
}

static inline void SPImcpDACtransmit(uint16_t data)
{
	// Send highbyte and wait for complete
	SPDR = highByte(data);
  asm("nop");
	while (!(SPSR & _BV(SPIF)))
		;
	// Send lowbyte and wait for complete
	SPDR = lowByte(data);
  asm("nop");
	while (!(SPSR & _BV(SPIF)))
		;
}

static inline void SPImcpDAClatch()
{
	MCP_DAC_LDAC_PORT &= ~_BV(MCP_DAC_LDAC_BIT);
	MCP_DAC_LDAC_PORT |= _BV(MCP_DAC_LDAC_BIT);
}

static inline void SPImcpDACsend(uint16_t data)
{
	MCP_DAC_CS_PORT &= ~_BV(MCP_DAC_CS_BIT);
  // Sanitize input data and add DAC config MSBs
  data &= 0x0FFF;
  data |= 0x7000;
	SPImcpDACtransmit(data);
	MCP_DAC_CS_PORT |= _BV(MCP_DAC_CS_BIT);
	// Do not latch immpediately, let's do it at the very beginning of the next interrupt to get consistent timing
}

static inline void SPImcpDAC2Asend(uint16_t data)
{
	MCP_DAC2_CS_PORT &= ~_BV(MCP_DAC2_CS_BIT);
	// Sanitize input data and add DAC config MSBs
	data &= 0x0FFF;
	data |= 0x7000;
	SPImcpDACtransmit(data);
	MCP_DAC2_CS_PORT |= _BV(MCP_DAC2_CS_BIT);
	SPImcpDAClatch();
}

static inline void SPImcpDAC2Bsend(uint16_t data)
{
	MCP_DAC2_CS_PORT &= ~_BV(MCP_DAC2_CS_BIT);
	// Sanitize input data and add DAC config MSBs
	data &= 0x0FFF;
	data |= 0xF000;
	SPImcpDACtransmit(data);
	MCP_DAC2_CS_PORT |= _BV(MCP_DAC2_CS_BIT);
	SPImcpDAClatch();
}

static inline void SPImcpDAC3Asend(uint16_t data)
{
  MCP_DAC3_CS_PORT &= ~_BV(MCP_DAC3_CS_BIT);
  // Sanitize input data and add DAC config MSBs
  data &= 0x0FFF;
  data |= 0x7000;
  SPImcpDACtransmit(data);
  MCP_DAC3_CS_PORT |= _BV(MCP_DAC3_CS_BIT);
  SPImcpDAClatch();
}

static inline void SPImcpDAC3Bsend(uint16_t data)
{
  MCP_DAC3_CS_PORT &= ~_BV(MCP_DAC3_CS_BIT);
  // Sanitize input data and add DAC config MSBs
  data &= 0x0FFF;
  data |= 0xF000;
  SPImcpDACtransmit(data);
  MCP_DAC3_CS_PORT |= _BV(MCP_DAC3_CS_BIT);
  SPImcpDAClatch();
}

#endif
