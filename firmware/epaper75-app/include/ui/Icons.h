#pragma once

#include <Adafruit_GFX.h>

#include "weather/WeatherText.h"

namespace ui {

// Weather glyphs from filled primitives: a cloud is a black silhouette with a
// white inset, so the outline survives the panel's 1-bit planes. The sun is
// drawn in the accent color. `size` is the nominal icon width in pixels.
void drawWeatherIcon(Adafruit_GFX& canvas, weather::IconKind kind,
                     int16_t centerX, int16_t centerY, int16_t size,
                     uint16_t ink, uint16_t accent, uint16_t paper);

void drawWifiIcon(Adafruit_GFX& canvas, int16_t centerX, int16_t baselineY,
                  uint16_t color, uint16_t paper, bool crossed);

void drawBatteryIcon(Adafruit_GFX& canvas, int16_t x, int16_t y,
                     int8_t percent, uint16_t color, uint16_t paper);

// A line of the given width drawn as two triangles.
void drawThickLine(Adafruit_GFX& canvas, float x0, float y0, float x1,
                   float y1, float width, uint16_t color);

}  // namespace ui
