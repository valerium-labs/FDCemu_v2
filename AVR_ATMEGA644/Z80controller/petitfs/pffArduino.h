#ifndef pffArduino_h
#define pffArduino_h
#include <avr/io.h>
#include <util/delay_basic.h>
#include "integer.h"

// SD pins (port B)
#define SD_CS_PIN 4
#define SD_MOSI_PIN 5
#define SD_SCK_PIN 7
#define SD_MISO_PIN 6

//SD CS line
#define SD_CS_PORT PORTB
#define SD_CS_DDR DDRB
#define SD_CS_BIT SD_CS_PIN

// Use SPI SCK divisor of 2 if nonzero else 4.
#define SPI_FCPU_DIV_2 1
//------------------------------------------------------------------------------
#define	FORWARD(d)	xmit(d)				/* Data forwarding function (console out) */
static void xmit(char d) {}  // Dummy write console
//------------------------------------------------------------------------------
static void spi_set_divisor(BYTE cardType) {
  if (!cardType) {
    // Set slow speed for initialization.
    SPCR = (1 << SPE) | (1 << MSTR) | 3;
    SPSR = 0;
  }  else {
    // Set high speed.
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR = SPI_FCPU_DIV_2 ? 1 << SPI2X : 0;
  }
}
//------------------------------------------------------------------------------
/** Send a byte to the card */
inline void xmit_spi(BYTE d) {SPDR = d; while(!(SPSR & (1 << SPIF)));}
//------------------------------------------------------------------------------
/** Receive a byte from the card */
inline BYTE rcv_spi (void) {xmit_spi(0XFF); return SPDR;}
//------------------------------------------------------------------------------


#define SD_CS_MASK (1 << SD_CS_BIT)
#define SELECT()  (SD_CS_PORT &= ~SD_CS_MASK)	 /* CS = L */
#define	DESELECT()	(SD_CS_PORT |= SD_CS_MASK)	/* CS = H */
#define	SELECTING	!(SD_CS_PORT & SD_CS_MASK)	  /* CS status (true:CS low) */

static void init_spi (void) {
  PORTB |= 1 << SD_CS_PIN;  // SS high
  DDRB  |= 1 << SD_CS_PIN;  // SS output mode
  DDRB  |= 1 << SD_MOSI_PIN;  // MOSI output mode
  DDRB  |= 1 << SD_SCK_PIN;  // SCK output mode
  SD_CS_DDR |= SD_CS_MASK;
  spi_set_divisor(0);
}

//------------------------------------------------------------------------------
static void dly_100us (void) {
  // each count delays four CPU cycles.
  _delay_loop_2(F_CPU/(40000));
}
#endif  // pffArduino_h
