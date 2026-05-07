#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>
#include <pgmspace.h>

#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 126
#define FONT_CHAR_COUNT 95

typedef struct {
  const uint8_t *data;
  uint8_t width;
  uint8_t height;
  uint8_t bytesPerRow;
  uint8_t bytesPerChar;
  uint8_t firstChar;
  uint8_t lastChar;
} BitmapFont;

extern const BitmapFont Font6x12;
extern const BitmapFont Font8x16;
extern const BitmapFont Font12x24;

uint32_t fontCharOffset(const BitmapFont *font, char c);

#endif
