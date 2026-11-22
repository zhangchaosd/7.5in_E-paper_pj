#include "weather/WeatherText.h"

namespace weather {
namespace {

struct Condition {
  uint16_t code;
  const char* text;
  IconKind icon;
  bool severe;
};

constexpr Condition kConditions[] = {
    {100, "晴", IconKind::Clear, false},
    {101, "多云", IconKind::PartlyCloudy, false},
    {102, "少云", IconKind::PartlyCloudy, false},
    {103, "晴间多云", IconKind::PartlyCloudy, false},
    {104, "阴", IconKind::Overcast, false},
    {150, "晴", IconKind::Clear, false},
    {151, "多云", IconKind::PartlyCloudy, false},
    {152, "少云", IconKind::PartlyCloudy, false},
    {153, "晴间多云", IconKind::PartlyCloudy, false},
    {300, "阵雨", IconKind::Rain, false},
    {301, "强阵雨", IconKind::HeavyRain, true},
    {302, "雷阵雨", IconKind::Thunder, false},
    {303, "强雷阵雨", IconKind::Thunder, true},
    {304, "雷阵雨伴有冰雹", IconKind::Thunder, true},
    {305, "小雨", IconKind::Rain, false},
    {306, "中雨", IconKind::Rain, false},
    {307, "大雨", IconKind::HeavyRain, true},
    {308, "极端降雨", IconKind::HeavyRain, true},
    {309, "毛毛雨", IconKind::Rain, false},
    {310, "暴雨", IconKind::HeavyRain, true},
    {311, "大暴雨", IconKind::HeavyRain, true},
    {312, "特大暴雨", IconKind::HeavyRain, true},
    {313, "冻雨", IconKind::Sleet, true},
    {314, "小到中雨", IconKind::Rain, false},
    {315, "中到大雨", IconKind::HeavyRain, true},
    {316, "大到暴雨", IconKind::HeavyRain, true},
    {317, "暴雨到大暴雨", IconKind::HeavyRain, true},
    {318, "大暴雨到特大暴雨", IconKind::HeavyRain, true},
    {350, "阵雨", IconKind::Rain, false},
    {351, "强阵雨", IconKind::HeavyRain, true},
    {399, "雨", IconKind::Rain, false},
    {400, "小雪", IconKind::Snow, false},
    {401, "中雪", IconKind::Snow, false},
    {402, "大雪", IconKind::Snow, true},
    {403, "暴雪", IconKind::Snow, true},
    {404, "雨夹雪", IconKind::Sleet, false},
    {405, "雨雪天气", IconKind::Sleet, false},
    {406, "阵雨夹雪", IconKind::Sleet, false},
    {407, "阵雪", IconKind::Snow, false},
    {408, "小到中雪", IconKind::Snow, false},
    {409, "中到大雪", IconKind::Snow, true},
    {410, "大到暴雪", IconKind::Snow, true},
    {456, "阵雨夹雪", IconKind::Sleet, false},
    {457, "阵雪", IconKind::Snow, false},
    {499, "雪", IconKind::Snow, false},
    {500, "薄雾", IconKind::Fog, false},
    {501, "雾", IconKind::Fog, false},
    {502, "霾", IconKind::Haze, false},
    {503, "扬沙", IconKind::Dust, false},
    {504, "浮尘", IconKind::Dust, false},
    {507, "沙尘暴", IconKind::Dust, true},
    {508, "强沙尘暴", IconKind::Dust, true},
    {509, "浓雾", IconKind::Fog, true},
    {510, "强浓雾", IconKind::Fog, true},
    {511, "中度霾", IconKind::Haze, false},
    {512, "重度霾", IconKind::Haze, true},
    {513, "严重霾", IconKind::Haze, true},
    {514, "大雾", IconKind::Fog, true},
    {515, "特强浓雾", IconKind::Fog, true},
    {900, "热", IconKind::Clear, false},
    {901, "冷", IconKind::Cloudy, false},
    {999, "未知", IconKind::Unknown, false},
};

const Condition* findCondition(uint16_t code) {
  for (const Condition& condition : kConditions) {
    if (condition.code == code) return &condition;
  }
  return nullptr;
}

}  // namespace

const char* conditionText(uint16_t code) {
  if (const Condition* c = findCondition(code)) return c->text;
  switch (code / 100) {
    case 1: return "多云";
    case 3: return "雨";
    case 4: return "雪";
    case 5: return "雾";
    default: return "未知";
  }
}

IconKind conditionIcon(uint16_t code) {
  if (const Condition* c = findCondition(code)) return c->icon;
  switch (code / 100) {
    case 1: return IconKind::PartlyCloudy;
    case 3: return IconKind::Rain;
    case 4: return IconKind::Snow;
    case 5: return IconKind::Fog;
    default: return IconKind::Unknown;
  }
}

bool conditionSevere(uint16_t code) {
  const Condition* c = findCondition(code);
  return c != nullptr && c->severe;
}

const char* windDirection(int16_t degree) {
  static const char* const kNames[8] = {"北", "东北", "东", "东南",
                                        "南", "西南", "西", "西北"};
  if (degree < 0) return "--";
  const int sector = ((degree % 360) * 2 + 45) / 90 % 8;
  return kNames[sector];
}

const char* aqiCategory(int16_t aqi) {
  if (aqi < 0) return "--";
  if (aqi <= 50) return "优";
  if (aqi <= 100) return "良";
  if (aqi <= 150) return "轻度污染";
  if (aqi <= 200) return "中度污染";
  if (aqi <= 300) return "重度污染";
  return "严重污染";
}

const char* aqiShort(int16_t aqi) {
  if (aqi < 0) return "--";
  if (aqi <= 50) return "优";
  if (aqi <= 100) return "良";
  if (aqi <= 150) return "轻度";
  if (aqi <= 200) return "中度";
  if (aqi <= 300) return "重度";
  return "严重";
}

bool aqiPoor(int16_t aqi) { return aqi >= 151; }

const char* alertColorText(app::AlertColor color) {
  switch (color) {
    case app::AlertColor::Blue: return "蓝色";
    case app::AlertColor::Yellow: return "黄色";
    case app::AlertColor::Orange: return "橙色";
    case app::AlertColor::Red: return "红色";
    case app::AlertColor::White: return "白色";
    default: return "";
  }
}

}  // namespace weather
