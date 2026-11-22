#include "ui/Icons.h"

#include <initializer_list>
#include <math.h>

namespace ui {
namespace {

int16_t r16(float value) { return static_cast<int16_t>(lroundf(value)); }

void cloudShape(Adafruit_GFX& canvas, float cx, float cy, float s, float inset,
                uint16_t color) {
  const float h = s * 0.36f;
  const float radius = h / 2 - inset;
  const float w = s - 2 * inset;
  if (w > 0 && radius > 0) {
    canvas.fillRoundRect(r16(cx - s / 2 + inset), r16(cy - h / 2 + inset),
                         r16(w), r16(h - 2 * inset), r16(radius), color);
  }
  const float r1 = s * 0.22f - inset;
  const float r2 = s * 0.28f - inset;
  if (r1 > 0) canvas.fillCircle(r16(cx - s * 0.16f), r16(cy - h * 0.45f), r16(r1), color);
  if (r2 > 0) canvas.fillCircle(r16(cx + s * 0.12f), r16(cy - h * 0.62f), r16(r2), color);
}

void cloud(Adafruit_GFX& canvas, float cx, float cy, float s, bool solid,
           uint16_t ink, uint16_t paper) {
  const float stroke = fmaxf(2.5f, s * 0.05f);
  cloudShape(canvas, cx, cy, s, 0, ink);
  if (!solid) cloudShape(canvas, cx, cy, s, stroke, paper);
}

void sun(Adafruit_GFX& canvas, float cx, float cy, float r, uint16_t color) {
  canvas.fillCircle(r16(cx), r16(cy), r16(r), color);
  const float width = fmaxf(2.0f, r * 0.24f);
  for (int i = 0; i < 8; ++i) {
    const float a = static_cast<float>(i) * static_cast<float>(M_PI) / 4.0f;
    drawThickLine(canvas, cx + cosf(a) * r * 1.45f, cy + sinf(a) * r * 1.45f,
                  cx + cosf(a) * r * 1.95f, cy + sinf(a) * r * 1.95f, width,
                  color);
  }
}

void drops(Adafruit_GFX& canvas, float cx, float cy, float s, uint16_t color) {
  const float width = fmaxf(2.0f, s * 0.05f);
  for (int i = 0; i < 3; ++i) {
    const float x = cx - s * 0.22f + static_cast<float>(i) * s * 0.22f;
    drawThickLine(canvas, x, cy, x - s * 0.08f, cy + s * 0.2f, width, color);
  }
}

void flakes(Adafruit_GFX& canvas, float cx, float cy, float s, uint16_t color) {
  const float r = fmaxf(2.0f, s * 0.05f);
  for (int i = 0; i < 3; ++i) {
    const float x = cx - s * 0.22f + static_cast<float>(i) * s * 0.22f;
    const float y = cy + (i == 1 ? s * 0.14f : s * 0.04f);
    canvas.fillCircle(r16(x), r16(y), r16(r), color);
  }
}

void bolt(Adafruit_GFX& canvas, float cx, float cy, float s, uint16_t color) {
  canvas.fillTriangle(r16(cx + s * 0.04f), r16(cy - s * 0.02f),
                      r16(cx - s * 0.12f), r16(cy + s * 0.16f),
                      r16(cx + s * 0.0f), r16(cy + s * 0.16f), color);
  canvas.fillTriangle(r16(cx + s * 0.06f), r16(cy + s * 0.12f),
                      r16(cx - s * 0.06f), r16(cy + s * 0.12f),
                      r16(cx - s * 0.08f), r16(cy + s * 0.32f), color);
}

void bars(Adafruit_GFX& canvas, float cx, float cy, float s, uint16_t color,
          bool dotted) {
  const float width = fmaxf(2.0f, s * 0.06f);
  for (int i = 0; i < 3; ++i) {
    const float y = cy - s * 0.18f + static_cast<float>(i) * s * 0.18f;
    const float inset = (i == 1) ? s * 0.1f : 0.0f;
    if (dotted) {
      for (float x = cx - s * 0.36f + inset; x < cx + s * 0.36f - inset;
           x += s * 0.16f) {
        canvas.fillCircle(r16(x), r16(y), r16(width * 0.7f), color);
      }
    } else {
      drawThickLine(canvas, cx - s * 0.36f + inset, y, cx + s * 0.36f - inset,
                    y, width, color);
    }
  }
}

}  // namespace

void drawThickLine(Adafruit_GFX& canvas, float x0, float y0, float x1,
                   float y1, float width, uint16_t color) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float length = sqrtf(dx * dx + dy * dy);
  if (length < 0.01f) return;
  const float nx = -dy / length * width / 2;
  const float ny = dx / length * width / 2;
  canvas.fillTriangle(r16(x0 + nx), r16(y0 + ny), r16(x0 - nx), r16(y0 - ny),
                      r16(x1 + nx), r16(y1 + ny), color);
  canvas.fillTriangle(r16(x0 - nx), r16(y0 - ny), r16(x1 - nx), r16(y1 - ny),
                      r16(x1 + nx), r16(y1 + ny), color);
}

void drawWeatherIcon(Adafruit_GFX& canvas, weather::IconKind kind,
                     int16_t centerX, int16_t centerY, int16_t size,
                     uint16_t ink, uint16_t accent, uint16_t paper) {
  const float cx = centerX;
  const float cy = centerY;
  const float s = size;
  using weather::IconKind;
  switch (kind) {
    case IconKind::Clear:
      sun(canvas, cx, cy, s * 0.24f, accent);
      break;
    case IconKind::PartlyCloudy:
      sun(canvas, cx + s * 0.18f, cy - s * 0.16f, s * 0.2f, accent);
      cloud(canvas, cx - s * 0.06f, cy + s * 0.1f, s * 0.82f, false, ink, paper);
      break;
    case IconKind::Cloudy:
      cloud(canvas, cx, cy, s * 0.9f, false, ink, paper);
      break;
    case IconKind::Overcast:
      cloud(canvas, cx + s * 0.12f, cy - s * 0.12f, s * 0.62f, true, ink, paper);
      cloud(canvas, cx - s * 0.06f, cy + s * 0.06f, s * 0.82f, false, ink, paper);
      break;
    case IconKind::Rain:
      cloud(canvas, cx, cy - s * 0.12f, s * 0.86f, false, ink, paper);
      drops(canvas, cx, cy + s * 0.14f, s, ink);
      break;
    case IconKind::HeavyRain:
      cloud(canvas, cx, cy - s * 0.12f, s * 0.86f, true, ink, paper);
      drops(canvas, cx, cy + s * 0.14f, s, accent);
      break;
    case IconKind::Thunder:
      cloud(canvas, cx, cy - s * 0.14f, s * 0.86f, true, ink, paper);
      bolt(canvas, cx, cy + s * 0.06f, s, accent);
      break;
    case IconKind::Snow:
      cloud(canvas, cx, cy - s * 0.12f, s * 0.86f, false, ink, paper);
      flakes(canvas, cx, cy + s * 0.18f, s, ink);
      break;
    case IconKind::Sleet:
      cloud(canvas, cx, cy - s * 0.12f, s * 0.86f, false, ink, paper);
      drawThickLine(canvas, cx - s * 0.16f, cy + s * 0.14f, cx - s * 0.22f,
                    cy + s * 0.32f, fmaxf(2.0f, s * 0.05f), ink);
      canvas.fillCircle(r16(cx + s * 0.1f), r16(cy + s * 0.24f),
                        r16(fmaxf(2.0f, s * 0.05f)), ink);
      break;
    case IconKind::Fog:
      bars(canvas, cx, cy, s, ink, false);
      break;
    case IconKind::Haze:
      bars(canvas, cx, cy, s, ink, true);
      break;
    case IconKind::Dust:
      bars(canvas, cx, cy, s, accent, true);
      break;
    case IconKind::Unknown:
    default:
      canvas.drawCircle(r16(cx), r16(cy), r16(s * 0.24f), ink);
      break;
  }
}

void drawWifiIcon(Adafruit_GFX& canvas, int16_t cx, int16_t by,
                  uint16_t color, uint16_t paper, bool crossed) {
  for (int16_t r : {14, 9, 4}) {
    canvas.drawCircle(cx, by, r, color);
    canvas.drawCircle(cx, by, r - 1, color);
  }
  canvas.fillRect(cx - 16, by + 1, 33, 16, paper);
  canvas.fillCircle(cx, by, 2, color);
  if (crossed) drawThickLine(canvas, cx - 12, by - 14, cx + 12, by + 2, 3, color);
}

void drawBatteryIcon(Adafruit_GFX& canvas, int16_t x, int16_t y,
                     int8_t percent, uint16_t color, uint16_t paper) {
  // 28 x 14 body with a terminal nub; fill proportional to the estimate.
  canvas.drawRect(x, y, 28, 14, color);
  canvas.drawRect(x + 1, y + 1, 26, 12, color);
  canvas.fillRect(x + 28, y + 4, 3, 6, color);
  canvas.fillRect(x + 2, y + 2, 24, 10, paper);
  if (percent > 0) {
    const int16_t w = static_cast<int16_t>(22 * (percent > 100 ? 100 : percent) / 100);
    canvas.fillRect(x + 3, y + 3, w < 2 ? 2 : w, 8, color);
  }
}

}  // namespace ui
