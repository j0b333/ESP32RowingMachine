/**
 * @file display_font5x7.h
 * @brief Tiny 5x7 ASCII font shared by mono and color renderers.
 */

#ifndef DISPLAY_FONT5X7_H
#define DISPLAY_FONT5X7_H

#include <stdint.h>

/* 5 column bytes per glyph, ASCII 0x20..0x7E (95 glyphs).
 * Bit0 of each byte is the topmost pixel. */
extern const uint8_t font5x7[95][5];

#endif
