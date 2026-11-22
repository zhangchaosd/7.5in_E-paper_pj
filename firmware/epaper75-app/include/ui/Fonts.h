#pragma once

#include <Adafruit_GFX.h>
#include <stdint.h>

namespace ui {

// One glyph of a generated 1-bit font. Offsets follow Adafruit GFX semantics:
// (xOffset, yOffset) is the top-left of the bitmap relative to the pen
// position on the baseline; yOffset is normally negative.
struct GlyphInfo {
  uint32_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  int8_t xOffset;
  int8_t yOffset;
  uint8_t xAdvance;
};

// Sorted BMP code points with parallel glyph records. Bitmaps are packed
// MSB-first, row-major, each glyph starting on a byte boundary.
struct FontData {
  const uint16_t* codes;
  const GlyphInfo* glyphs;
  const uint8_t* bitmap;
  uint16_t count;
  int8_t ascent;
  int8_t descent;
};

// Generated in src/ui/fonts/ by tools/gen/gen_fonts.sh.
extern const FontData kFontSmall;     // 13 px regular: calendar lunar line
extern const FontData kFontSmallBold; // 13 px bold: chips
extern const FontData kFontBody;      // 17 px regular, GB2312 level 1
extern const FontData kFontBodyBold;  // 17 px bold
extern const FontData kFontNumber;    // 22 px bold ASCII: calendar days
extern const FontData kFontTitle;     // 26 px bold, GB2312 level 1
extern const FontData kFontHeading;   // 36 px bold: month title, units
extern const FontData kFontHero;      // 80 px bold digits

const GlyphInfo* findGlyph(const FontData& font, uint16_t code);

// Decodes one UTF-8 code point and advances *cursor. Invalid sequences yield
// U+FFFD and consume one byte so that drawing always terminates.
uint16_t nextCodePoint(const char** cursor);

// Width of a UTF-8 string in pixels (missing glyphs use the '?' advance).
int16_t textWidth(const FontData& font, const char* text);

// Draws a UTF-8 string with its baseline at y. Returns the end pen x.
int16_t drawText(Adafruit_GFX& canvas, const FontData& font, int16_t x,
                 int16_t y, const char* text, uint16_t color);

enum class Align : uint8_t { Left, Center, Right };
int16_t drawTextAligned(Adafruit_GFX& canvas, const FontData& font,
                        int16_t x, int16_t y, const char* text,
                        uint16_t color, Align align);

}  // namespace ui
