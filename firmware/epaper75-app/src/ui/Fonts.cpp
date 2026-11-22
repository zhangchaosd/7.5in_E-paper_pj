#include "ui/Fonts.h"

namespace ui {

const GlyphInfo* findGlyph(const FontData& font, uint16_t code) {
  int32_t lo = 0;
  int32_t hi = static_cast<int32_t>(font.count) - 1;
  while (lo <= hi) {
    const int32_t mid = (lo + hi) / 2;
    const uint16_t value = font.codes[mid];
    if (value == code) return &font.glyphs[mid];
    if (value < code) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

uint16_t nextCodePoint(const char** cursor) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(*cursor);
  const uint8_t b0 = p[0];
  if (b0 < 0x80) {
    *cursor += 1;
    return b0;
  }
  if ((b0 & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
    *cursor += 2;
    return static_cast<uint16_t>(((b0 & 0x1f) << 6) | (p[1] & 0x3f));
  }
  if ((b0 & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
    *cursor += 3;
    return static_cast<uint16_t>(((b0 & 0x0f) << 12) | ((p[1] & 0x3f) << 6) |
                                 (p[2] & 0x3f));
  }
  if ((b0 & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
      (p[3] & 0xc0) == 0x80) {
    *cursor += 4;  // Outside the BMP: rendered as a replacement glyph.
    return 0xfffd;
  }
  *cursor += 1;
  return 0xfffd;
}

namespace {

const GlyphInfo* glyphOrFallback(const FontData& font, uint16_t code) {
  const GlyphInfo* glyph = findGlyph(font, code);
  if (glyph == nullptr) glyph = findGlyph(font, '?');
  return glyph;
}

}  // namespace

int16_t textWidth(const FontData& font, const char* text) {
  if (text == nullptr) return 0;
  int16_t width = 0;
  const char* cursor = text;
  while (*cursor != '\0') {
    const GlyphInfo* glyph = glyphOrFallback(font, nextCodePoint(&cursor));
    if (glyph != nullptr) width = static_cast<int16_t>(width + glyph->xAdvance);
  }
  return width;
}

int16_t drawText(Adafruit_GFX& canvas, const FontData& font, int16_t x,
                 int16_t y, const char* text, uint16_t color) {
  if (text == nullptr) return x;
  const char* cursor = text;
  while (*cursor != '\0') {
    const GlyphInfo* glyph = glyphOrFallback(font, nextCodePoint(&cursor));
    if (glyph == nullptr) continue;
    const uint8_t* bits = font.bitmap + glyph->bitmapOffset;
    const int16_t left = static_cast<int16_t>(x + glyph->xOffset);
    const int16_t top = static_cast<int16_t>(y + glyph->yOffset);
    uint32_t bit = 0;
    for (uint8_t row = 0; row < glyph->height; ++row) {
      // Emit horizontal runs instead of single pixels: far fewer driver calls.
      int16_t runStart = -1;
      for (uint8_t col = 0; col < glyph->width; ++col, ++bit) {
        const bool on = (bits[bit >> 3] & (0x80 >> (bit & 7))) != 0;
        if (on && runStart < 0) runStart = col;
        if (!on && runStart >= 0) {
          canvas.drawFastHLine(left + runStart, top + row, col - runStart,
                               color);
          runStart = -1;
        }
      }
      if (runStart >= 0) {
        canvas.drawFastHLine(left + runStart, top + row,
                             glyph->width - runStart, color);
      }
      // Each glyph row is packed continuously; only the glyph end is padded.
    }
    x = static_cast<int16_t>(x + glyph->xAdvance);
  }
  return x;
}

int16_t drawTextAligned(Adafruit_GFX& canvas, const FontData& font,
                        int16_t x, int16_t y, const char* text,
                        uint16_t color, Align align) {
  int16_t start = x;
  if (align != Align::Left) {
    const int16_t width = textWidth(font, text);
    start = align == Align::Center ? static_cast<int16_t>(x - width / 2)
                                   : static_cast<int16_t>(x - width);
  }
  return drawText(canvas, font, start, y, text, color);
}

}  // namespace ui
