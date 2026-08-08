#pragma once

#define IO_LED 28
#define IO_BUZ 22

/* I2C bus. Pin pairs are fixed by the RP2040 mux: SDA must be an even GPIO and
 * SCL the next odd one. GPIO 14/15 belong to i2c1; change to match the board. */
#define IO_SYS_I2C_INDEX 1 /* 0 = i2c0, 1 = i2c1 */
#define IO_SYS_I2C_SDA 14
#define IO_SYS_I2C_SCL 15
#define IO_SYS_I2C_BAUD (400 * 1000)

#define IO_PCF8575_IRQ 26
#define PCF8575_0_ADDR 0x20
#define PCF8575_1_ADDR 0x21
// PCF8575 for 24 btn, 4 toggles, 4 leds
#define PCF8575_BTN_MASK 0x00ff
#define PCF8575_TOGGLE_MASK 0x0f00
#define PCF8575_LED_MASK 0xf000
/* The masks above describe this chip; the other one is 16 more buttons.
 * 16 + 8 = 24 buttons, 4 toggles, 4 LEDs. Set to 0 if the roles are swapped. */
#define PCF8575_LED_CHIP 1

// SPI bus for slave
// Audio (SPI0) (input only)
#define IO_ASPI_SCK 3
#define IO_ASPI_MOSI 4
#define IO_ASPI_MISO 5
#define IO_ASPI_CS 5
// Data (SPI1) + IRQ signal out to main controller
#define IO_DSPI_SCK 10
#define IO_DSPI_MOSI 11
#define IO_DSPI_MISO 12
#define IO_DSPI_CS 13
#define IO_DSPI_IRQ 27

// R2R+74HC595 (Stereo 16 bit, 8 bit per channel)
#define IO_R2R_DS 9
#define IO_R2R_OE 8
#define IO_R2R_ST 7
#define IO_R2R_SH 6