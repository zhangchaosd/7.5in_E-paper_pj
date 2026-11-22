#include "app/Models.h"

namespace app {

const char* weatherErrorName(WeatherError error) {
  switch (error) {
    case WeatherError::None: return "none";
    case WeatherError::NotConfigured: return "not_configured";
    case WeatherError::NoWifi: return "no_wifi";
    case WeatherError::ClockNotSynced: return "clock_not_synced";
    case WeatherError::Tls: return "tls";
    case WeatherError::Http: return "http";
    case WeatherError::Unauthorized: return "unauthorized";
    case WeatherError::RateLimited: return "rate_limited";
    case WeatherError::Decompress: return "decompress";
    case WeatherError::Parse: return "parse";
    case WeatherError::LowBattery: return "low_battery";
    default: return "unknown";
  }
}

}  // namespace app
