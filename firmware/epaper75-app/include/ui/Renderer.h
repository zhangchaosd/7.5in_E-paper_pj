#pragma once

#include <Adafruit_GFX.h>

#include "app/Models.h"

namespace ui {

// Pure drawing code. Renderer never initializes, refreshes, powers, or sleeps
// the panel; EpdController owns the complete hardware lifecycle. render() is
// called once per GxEPD2 page and must be deterministic for a given model.
class Renderer {
 public:
  void render(Adafruit_GFX& canvas, const app::UiModel& model) const;

 private:
  void drawHeader(Adafruit_GFX& canvas, const app::UiModel& model,
                  const char* title, const char* subtitle) const;
  void drawToday(Adafruit_GFX& canvas, const app::UiModel& model) const;
  void drawCurrentWeather(Adafruit_GFX& canvas,
                          const app::UiModel& model) const;
  void drawCalendar(Adafruit_GFX& canvas, const app::UiModel& model) const;
  void drawForecast(Adafruit_GFX& canvas, const app::UiModel& model) const;
  void drawSystemPage(Adafruit_GFX& canvas, const app::UiModel& model) const;
};

}  // namespace ui
