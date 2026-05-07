#include "ssd1680.h"

#include <string.h>

static constexpr uint8_t PIN_SCK = 12;
static constexpr uint8_t PIN_MOSI = 11;
static constexpr uint8_t PIN_RES = 10;
static constexpr uint8_t PIN_DC = 13;
static constexpr uint8_t PIN_CS = 14;
static constexpr uint8_t PIN_BUSY = 9;

uint8_t dispData[BUFFER_BYTES];

static void writeByte(uint8_t value)
{
    digitalWrite(PIN_CS, LOW);
    for (uint8_t i = 0; i < 8; i++)
    {
        digitalWrite(PIN_SCK, LOW);
        digitalWrite(PIN_MOSI, (value & 0x80) ? HIGH : LOW);
        digitalWrite(PIN_SCK, HIGH);
        value <<= 1;
    }
    digitalWrite(PIN_CS, HIGH);
}

static void writeCommand(uint8_t command)
{
    digitalWrite(PIN_DC, LOW);
    writeByte(command);
    digitalWrite(PIN_DC, HIGH);
}

static void writeData(uint8_t data)
{
    digitalWrite(PIN_DC, HIGH);
    writeByte(data);
}

static void waitBusy()
{
    while (digitalRead(PIN_BUSY) == HIGH)
    {
    }
    delayMicroseconds(100);
}

static void resetPanel()
{
    delay(100);
    digitalWrite(PIN_RES, HIGH);
    delay(10);
    digitalWrite(PIN_RES, LOW);
    delay(10);
    digitalWrite(PIN_RES, HIGH);
    delay(10);
    waitBusy();
    writeCommand(0x12);
    waitBusy();
}

static const BitmapFont *fontForSize(uint16_t sizey)
{
    switch (sizey)
    {
    case 12:
        return &Font6x12;
    case 16:
        return &Font8x16;
    case 24:
        return &Font12x24;
    default:
        return &Font6x12;
    }
}

static void drawChar(uint16_t x, uint16_t y, char c, const BitmapFont *font, uint8_t color)
{
    uint32_t offset = fontCharOffset(font, c);

    for (uint8_t row = 0; row < font->height; row++)
    {
        for (uint8_t col = 0; col < font->width; col++)
        {
            uint32_t byteIndex = offset + (uint32_t)row * font->bytesPerRow + (col / 8);
            uint8_t bitmapByte = pgm_read_byte(&font->data[byteIndex]);
            uint8_t bitMask = 0x80 >> (col % 8);
            displayDrawPixel(x + col, y + row, (bitmapByte & bitMask) ? color : !color);
        }
    }
}

void displayInit(void)
{
    pinMode(PIN_SCK, OUTPUT);
    pinMode(PIN_MOSI, OUTPUT);
    pinMode(PIN_RES, OUTPUT);
    pinMode(PIN_DC, OUTPUT);
    pinMode(PIN_CS, OUTPUT);
    pinMode(PIN_BUSY, INPUT);

    digitalWrite(PIN_SCK, LOW);
    digitalWrite(PIN_MOSI, LOW);
    digitalWrite(PIN_RES, HIGH);
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_CS, HIGH);

    displayClear();
    resetPanel();

    writeCommand(0x01);
    writeData(0xF9);
    writeData(0x00);
    writeData(0x00);

    writeCommand(0x11);
    writeData(0x03);

    writeCommand(0x44);
    writeData(0x00);
    writeData(0x0F);

    writeCommand(0x45);
    writeData(0x00);
    writeData(0x00);
    writeData(0xF9);
    writeData(0x00);

    writeCommand(0x3C);
    writeData(0x01);
    waitBusy();

    writeCommand(0x18);
    writeData(0x80);

    writeCommand(0x4E);
    writeData(0x00);

    writeCommand(0x4F);
    writeData(0x00);
    writeData(0x00);

    waitBusy();
}

void displayClear(void)
{
    memset(dispData, 0x00, sizeof(dispData));
}

void displayDrawPixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (x >= SCR_WIDTH || y >= SCR_HEIGHT)
    {
        return;
    }

    uint16_t xpoint = y;
    uint16_t ypoint = SCR_WIDTH - x - 1;
    uint32_t addr = (uint32_t)xpoint / 8 + (uint32_t)ypoint * ROW_BYTES;
    uint8_t mask = 0x80 >> (xpoint % 8);

    if (color == BLACK)
    {
        dispData[addr] |= mask;
    }
    else
    {
        dispData[addr] &= (uint8_t)~mask;
    }
}

void displayDrawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap, uint16_t width, uint16_t height, uint8_t color)
{
    uint16_t bytesPerRow = (width + 7) / 8;

    for (uint16_t row = 0; row < height; row++)
    {
        for (uint16_t col = 0; col < width; col++)
        {
            uint32_t byteIndex = (uint32_t)row * bytesPerRow + (col / 8);
            uint8_t bitmapByte = pgm_read_byte(&bitmap[byteIndex]);
            uint8_t bitMask = 0x80 >> (col % 8);

            if (bitmapByte & bitMask)
            {
                displayDrawPixel(x + col, y + row, color);
            }
        }
    }
}

void displayDrawString(uint16_t x, uint16_t y, const char *s, uint8_t color, uint16_t sizey)
{
    const BitmapFont *font = fontForSize(sizey);

    while (*s >= ' ' && *s <= '~')
    {
        drawChar(x, y, *s, font, color);
        x += font->width;
        s++;
    }
}

void displayUpdate(void)
{
    writeCommand(0x3C);
    writeData(0x01);

    writeCommand(0x4E);
    writeData(0x00);

    writeCommand(0x4F);
    writeData(0x00);
    writeData(0x00);

    writeCommand(0x24);
    for (uint32_t i = 0; i < BUFFER_BYTES; i++)
    {
        writeData((uint8_t)~dispData[i]);
    }

    writeCommand(0x22);
    writeData(0xF4);
    writeCommand(0x20);
    waitBusy();
}

void displayDeepSleep(void)
{
    writeCommand(0x10);
    writeData(0x03);
    writeCommand(0x3C);
    writeData(0x01);
    delay(20);
}

