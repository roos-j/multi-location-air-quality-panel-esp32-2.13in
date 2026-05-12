#ifndef SSD1680_H
#define SSD1680_H

#include <Arduino.h>
#include <stdint.h>
#include <pgmspace.h>
#include "fonts.h"

#define SCR_WIDTH 250
#define SCR_HEIGHT 122
#define ROW_BYTES ((SCR_HEIGHT + 7) / 8)
#define BUFFER_BYTES (ROW_BYTES * SCR_WIDTH)

#define WHITE 0xFF
#define BLACK 0x00

extern uint8_t dispData[BUFFER_BYTES];

void displayInit(void);
void displayClear(void);
void displayDrawPixel(uint16_t x, uint16_t y, uint8_t color);
void displayDrawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap, uint16_t width, uint16_t height, uint8_t color);
void displayDrawString(uint16_t x, uint16_t y, const char *s, uint8_t color, uint16_t sizey);
void displayDrawCenteredString(uint16_t y, const char *s, uint8_t color, uint16_t sizey);
void displayDeepSleep(void);
void displayUpdate(void);

#endif
