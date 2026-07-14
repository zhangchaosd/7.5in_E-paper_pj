#pragma once

#include <Adafruit_GFX.h>

#include "app/Models.h"

namespace ui {

// Pure drawing code. Renderer never initializes, refreshes, powers, or sleeps
// the panel; EpdController owns the complete hardware lifecycle.
class Renderer {
 public:
  void render(Adafruit_GFX& canvas, const app::UiModel& model) const;

 private:
  void drawStatusBar(Adafruit_GFX& canvas,
                     const app::UiModel& model) const;
  void drawDashboard(Adafruit_GFX& canvas,
                     const app::UiModel& model) const;
  void drawCodexUsage(Adafruit_GFX& canvas,
                      const app::UiModel& model) const;
  void drawLatencyRow(Adafruit_GFX& canvas,
                      const app::LatencyTargetSnapshot& target,
                      size_t targetIndex, int16_t x, int16_t y,
                      int16_t width) const;
  void drawSystemPage(Adafruit_GFX& canvas,
                      const app::UiModel& model) const;
  void drawProgress(Adafruit_GFX& canvas, int16_t x, int16_t y,
                    int16_t width, int16_t percent, bool critical) const;
};

}  // namespace ui
