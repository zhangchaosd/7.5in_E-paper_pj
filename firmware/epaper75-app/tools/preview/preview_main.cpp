// Host preview: renders the real ui::Renderer output to 800x480 BMP files.
//   tools/preview/build_preview.sh <output-dir>
#include <Adafruit_GFX.h>
#include <GxEPD2.h>
#include <cstdio>
#include <vector>
#include "cal/Calendar.h"
#include "ui/Renderer.h"

class PreviewCanvas : public Adafruit_GFX {
 public:
  PreviewCanvas() : Adafruit_GFX(800, 480), pixels(800 * 480, 0) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= 800 || y >= 480) return;
    pixels[y * 800 + x] = c == GxEPD_WHITE ? 0 : (c == GxEPD_RED ? 2 : 1);
  }
  bool save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t row = 800 * 3, size = 54 + row * 480;
    uint8_t h[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) h[o + i] = (v >> (8 * i)) & 0xff; };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, 800); put32(22, 480);
    h[26] = 1; h[28] = 24; put32(34, row * 480);
    fwrite(h, 1, 54, f);
    for (int y = 479; y >= 0; --y)
      for (int x = 0; x < 800; ++x) {
        const uint8_t p = pixels[y * 800 + x];
        const uint8_t bgr[3] = {uint8_t(p == 0 ? 255 : 0), uint8_t(p == 0 ? 255 : 0), uint8_t(p == 0 ? 255 : (p == 2 ? 212 : 0))};
        fwrite(bgr, 1, 3, f);
      }
    fclose(f);
    return true;
  }
  std::vector<uint8_t> pixels;
};

static int64_t epochAt(int y, int mo, int d, int h, int mi) {
  return int64_t(cal::daysFromCivil(y, mo, d)) * 86400 + h * 3600 + mi * 60 - 8 * 3600;
}

static app::UiModel sample() {
  app::UiModel m;
  m.configured = true;
  m.clockSynced = true;
  m.nowEpoch = epochAt(2026, 11, 22, 18, 40);
  m.place.valid = true;
  app::copyText(m.place.name, "东城区");
  app::copyText(m.place.city, "北京");
  auto& w = m.weather;
  w.availability = app::DataAvailability::Available;
  w.fetchedEpoch = epochAt(2026, 11, 22, 18, 30);
  w.now = {true, 14, 12, 101, 68, 45, 3};
  const struct { int d; int lo, hi; uint16_t code; int pop; } days[7] = {
      {22, 11, 18, 101, 10}, {23, 11, 15, 305, 60}, {24, 9, 13, 104, 20}, {25, 8, 14, 101, 0},
      {26, 7, 15, 100, 0}, {27, 9, 17, 100, 0}, {28, 10, 14, 310, 90}};
  for (int i = 0; i < 7; ++i) {
    auto& f = w.days[i];
    f.valid = true; f.year = 2026; f.month = 11; f.day = days[i].d;
    f.minC = days[i].lo; f.maxC = days[i].hi; f.codeDay = days[i].code; f.codeNight = days[i].code;
    f.precipitationPercent = days[i].pop; f.sunriseMinute = 6 * 60 + 31; f.sunsetMinute = 16 * 60 + 53;
  }
  w.air = {true, 52};
  m.telemetry.sensorOk = true; m.telemetry.temperatureC = 23.4f; m.telemetry.humidityPercent = 46;
  m.telemetry.batteryMillivolts = 3980; m.telemetry.batteryPercent = 78;
  m.network.connected = true; m.network.rssi = -58;
  m.nextWakeMinutes = 140; m.wakeCount = 37; m.uptimeSeconds = 74;
  return m;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  ui::Renderer renderer;
  char path[512];
  auto out = [&](const char* name, const app::UiModel& model) {
    PreviewCanvas canvas;
    renderer.render(canvas, model);
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    canvas.save(path);
  };
  app::UiModel home = sample();
  out("home", home);

  app::UiModel alert = sample();
  alert.weather.alert.active = true;
  alert.weather.alert.color = app::AlertColor::Blue;
  app::copyText(alert.weather.alert.eventName, "大风");
  alert.weather.alert.count = 1;
  alert.telemetry.batteryPercent = 12;
  alert.weather.now.code = 307;
  alert.weather.air.aqi = 168;
  out("home-alert-lowbat", alert);

  app::UiModel stale = sample();
  stale.nowEpoch = epochAt(2027, 2, 6, 9, 5);
  stale.weather.fetchedEpoch = epochAt(2027, 2, 5, 21, 0);
  stale.weather.availability = app::DataAvailability::Unavailable;
  stale.weather.lastError = app::WeatherError::NoWifi;
  out("home-stale-springfestival", stale);

  app::UiModel fresh;
  out("home-unconfigured", fresh);

  app::UiModel system = sample();
  system.page = app::Page::System;
  out("system", system);
  return 0;
}
