#pragma once

#include "driver/gpio.h"

/*
 * Current active hardware profile.
 *
 * Keep physical pin assignments here. Driver modules may define bit packing,
 * timing, and protocol behavior, but they should not duplicate GPIO numbers.
 */

#define GBC_LCD_GPIO_CLS GPIO_NUM_3
#define GBC_LCD_GPIO_G5 GPIO_NUM_6
#define GBC_LCD_GPIO_G4 GPIO_NUM_5
#define GBC_LCD_GPIO_G3 GPIO_NUM_4
#define GBC_LCD_GPIO_G2 GPIO_NUM_10
#define GBC_LCD_GPIO_G1 GPIO_NUM_11
#define GBC_LCD_GPIO_G0 GPIO_NUM_12
#define GBC_LCD_GPIO_R5 GPIO_NUM_13
#define GBC_LCD_GPIO_R4 GPIO_NUM_14
#define GBC_LCD_GPIO_R3 GPIO_NUM_15
#define GBC_LCD_GPIO_R2 GPIO_NUM_16
#define GBC_LCD_GPIO_R1 GPIO_NUM_17
#define GBC_LCD_GPIO_R0 GPIO_NUM_18
#define GBC_LCD_GPIO_SPL GPIO_NUM_19
#define GBC_LCD_GPIO_PS GPIO_NUM_20
#define GBC_LCD_GPIO_LP GPIO_NUM_21
#define GBC_LCD_GPIO_DCLK GPIO_NUM_22
#define GBC_LCD_GPIO_SPS GPIO_NUM_33
#define GBC_LCD_GPIO_B0 GPIO_NUM_36
#define GBC_LCD_GPIO_B1 GPIO_NUM_45
#define GBC_LCD_GPIO_B2 GPIO_NUM_46
#define GBC_LCD_GPIO_B3 GPIO_NUM_47
#define GBC_LCD_GPIO_B4 GPIO_NUM_48
#define GBC_LCD_GPIO_B5 GPIO_NUM_50

#define SPI_LCD_GPIO_CS GPIO_NUM_52
#define SPI_LCD_GPIO_MOSI GPIO_NUM_31
#define SPI_LCD_GPIO_SCLK GPIO_NUM_28
#define SPI_LCD_GPIO_RESET GPIO_NUM_29
#define SPI_LCD_GPIO_DC GPIO_NUM_53

_Static_assert(GBC_LCD_GPIO_G5 != SPI_LCD_GPIO_CS, "GBC G5 conflicts with SPI LCD CS");
_Static_assert(GBC_LCD_GPIO_G4 != SPI_LCD_GPIO_MOSI, "GBC G4 conflicts with SPI LCD MOSI");
_Static_assert(GBC_LCD_GPIO_G3 != SPI_LCD_GPIO_SCLK, "GBC G3 conflicts with SPI LCD SCLK");
_Static_assert(GBC_LCD_GPIO_DCLK != SPI_LCD_GPIO_SCLK, "GBC DCLK conflicts with SPI LCD SCLK");
_Static_assert(GBC_LCD_GPIO_SPS != SPI_LCD_GPIO_CS, "GBC SPS conflicts with SPI LCD CS");
