// Epaper75 0.6.x: calendar + QWeather dashboard with a deep-sleep power plan.
//
// Every boot is one of:
//   - timer wake: run the scheduled job (full update or alert check), redraw
//     only if the visible page changed, then sleep until the next slot;
//   - button wake (Upper/Middle) or cold boot (power, EN): stay awake for an
//     interactive window that accepts buttons and USB serial commands;
// and always ends in deep sleep. All work runs on the Arduino loop task; the
// panel is driven only through ui::EpdController.
#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "app/DisplaySignature.h"
#include "app/Models.h"
#include "app/Schedule.h"
#include "board/Board.h"
#include "board/BoardConfig.h"
#include "net/Connectivity.h"
#include "net/SettingsStore.h"
#include "net/WeatherClient.h"
#include "ui/EpdController.h"

namespace {

// ---------------------------------------------------------------------------
// State that survives deep sleep (lost on power loss / EN reset).
struct RtcState {
  uint32_t magic;
  uint32_t wakeCount;
  app::WeatherSnapshot weather;
  app::PlaceSnapshot place;
  uint32_t displayedSignature;
  bool displayedValid;
  uint8_t displayedPage;
  int64_t planEpoch;
  uint8_t planKind;
  uint8_t retries;
};
constexpr uint32_t kRtcMagic = 0x45503036;  // "EP06"
// Raw storage on purpose: RtcState has default member initializers, and a
// global object of that type would be re-constructed by the C++ runtime on
// every deep-sleep wake, wiping the cache. It is constructed explicitly only
// on a cold boot (see setup()).
alignas(8) RTC_DATA_ATTR uint8_t rtcStorage[sizeof(RtcState)];
RtcState& rtc = *reinterpret_cast<RtcState*>(rtcStorage);

board::Board deviceBoard;
net::SettingsStore settingsStore;
net::Connectivity connectivity;
ui::EpdController epd;
net::WeatherSettings weatherSettings;

app::TelemetrySnapshot telemetry;
app::NetworkSnapshot lastNetwork;
app::Page page = app::Page::Home;
bool interactive = false;
bool refreshPending = false;
bool forceRefresh = false;
uint32_t lastActivityMs = 0;
uint32_t lastRefreshEndMs = 0;
char serialLine[320] = {};
size_t serialLength = 0;

// ---------------------------------------------------------------------------
bool clockSynced() { return net::clockPlausible(); }
int64_t nowEpoch() { return static_cast<int64_t>(time(nullptr)); }

bool batteryLow() {
  return telemetry.batteryMillivolts >= board::kBatteryAbsentMv &&
         telemetry.batteryMillivolts < board::kBatteryCutoffMv;
}

// Only the battery is sampled; indoor temperature/humidity are not shown, so
// the SHT30 stays unpowered (GPIO25 is raised only for the ADC sample).
void sampleTelemetry() {
  const board::BatteryReading battery = deviceBoard.readBattery();
  telemetry.batteryMillivolts = battery.batteryMillivolts;
  telemetry.batteryPercent = battery.batteryPercent;
}

app::UiModel buildModel() {
  app::UiModel model;
  model.weather = rtc.weather;
  model.place = rtc.place;
  model.telemetry = telemetry;
  model.network = lastNetwork;
  model.clockSynced = clockSynced();
  model.nowEpoch = nowEpoch();
  model.configured = weatherSettings.ready();
  model.page = page;
  model.wakeCount = rtc.wakeCount;
  model.uptimeSeconds = millis() / 1000U;
  if (model.clockSynced) {
    const int64_t delta = rtc.planEpoch - model.nowEpoch;
    model.nextWakeMinutes = delta > 0 ? static_cast<uint32_t>(delta / 60) : 0;
  }
  return model;
}

// Full refresh only when the visible content differs (or when forced).
bool updateDisplay(bool force) {
  if (epd.isFaultLocked()) {
    Serial.println("[display] fault-locked; skipped");
    return false;
  }
  const app::UiModel model = buildModel();
  const uint32_t signature = app::visibleSignature(model);
  if (!force && rtc.displayedValid && rtc.displayedSignature == signature &&
      rtc.displayedPage == static_cast<uint8_t>(page)) {
    Serial.println("[display] unchanged; no refresh");
    return true;
  }
  Serial.printf("[display] full refresh page %u\n", static_cast<unsigned>(page));
  const uint32_t started = millis();
  const bool ok = epd.refresh(model);
  lastRefreshEndMs = millis();
  if (ok) {
    rtc.displayedSignature = signature;
    rtc.displayedPage = static_cast<uint8_t>(page);
    rtc.displayedValid = true;
    Serial.printf("[display] completed in %lu ms\n",
                  static_cast<unsigned long>(lastRefreshEndMs - started));
  } else {
    rtc.displayedValid = false;
    Serial.printf("[display] failed: %s (journal=%s)\n",
                  ui::epdErrorName(epd.lastError()),
                  ui::panelJournalStateName(epd.journalState()));
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Network work. Returns true when the requested data was fetched.
enum class FetchMode : uint8_t { Full, AlertsThenFullIfChanged };

bool runFetch(FetchMode mode) {
  if (batteryLow()) {
    rtc.weather.lastError = app::WeatherError::LowBattery;
    Serial.println("[weather] battery low; radio stays off");
    return false;
  }
  if (!connectivity.connect(board::kWifiConnectTimeoutMs)) {
    lastNetwork = app::NetworkSnapshot();
    connectivity.disconnect();
    rtc.weather.availability = app::DataAvailability::Unavailable;
    rtc.weather.lastError = app::WeatherError::NoWifi;
    Serial.println("[wifi] connect failed");
    return false;
  }
  lastNetwork = connectivity.snapshot();
  const bool synced = connectivity.syncClock(board::kClockSyncTimeoutMs);
  Serial.printf("[clock] %s\n", synced ? "synchronized" : "sync failed");
  if (!clockSynced()) {
    connectivity.disconnect();
    rtc.weather.availability = app::DataAvailability::Unavailable;
    rtc.weather.lastError = app::WeatherError::ClockNotSynced;
    return false;
  }
  if (!weatherSettings.ready()) {
    connectivity.disconnect();
    rtc.weather.lastError = app::WeatherError::NotConfigured;
    return true;  // the clock (calendar) was still updated
  }

  net::WeatherClient client(weatherSettings);
  const int64_t now = nowEpoch();
  app::WeatherError error = app::WeatherError::None;
  bool doFull = mode == FetchMode::Full;

  if (!doFull) {
    app::WeatherAlert alert;
    error = client.fetchAlerts(now, alert);
    if (error == app::WeatherError::None) {
      doFull = alert.key != rtc.weather.alert.key ||
               alert.active != rtc.weather.alert.active;
      Serial.printf("[weather] alerts: %u active%s\n", alert.count,
                    doFull ? " (changed)" : "");
    }
  }
  if (doFull) {
    error = client.fetchAll(now, rtc.weather);
    Serial.printf("[weather] full fetch: %s (http %d, heap %lu, last body %u B)\n",
                  app::weatherErrorName(error), client.lastHttpCode(),
                  static_cast<unsigned long>(client.heapBeforeConnect()),
                  static_cast<unsigned>(client.lastBodyLength()));
    if (client.lastTransportError()[0] != '\0') {
      Serial.printf("[weather] transport: %s\n", client.lastTransportError());
    }
  }
  if (error == app::WeatherError::None && !rtc.place.valid) {
    app::PlaceSnapshot place;
    if (client.lookupPlace(now, place) == app::WeatherError::None) {
      rtc.place = place;
      net::PlaceCache cache;
      cache.place = place;
      cache.latitudeCenti = weatherSettings.latitudeCenti;
      cache.longitudeCenti = weatherSettings.longitudeCenti;
      settingsStore.savePlace(cache);
    }
  }
  connectivity.disconnect();
  if (error != app::WeatherError::None) {
    rtc.weather.availability = app::DataAvailability::Unavailable;
    rtc.weather.lastError = error;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
void scheduleNext(bool fetchFailed) {
  const int64_t now = nowEpoch();
  if (!clockSynced()) {
    rtc.planEpoch = now + app::kUnsyncedRetrySeconds;
    rtc.planKind = static_cast<uint8_t>(app::WakeKind::FullUpdate);
    return;
  }
  if (batteryLow()) {
    rtc.planEpoch = now + app::kLowBatterySleepSeconds;
    rtc.planKind = static_cast<uint8_t>(app::WakeKind::FullUpdate);
    return;
  }
  if (fetchFailed && weatherSettings.ready() &&
      rtc.retries < app::kMaxRetriesPerSlot) {
    ++rtc.retries;
    rtc.planEpoch = now + app::kRetryDelaySeconds;
    return;  // keep the same kind
  }
  rtc.retries = 0;
  const app::WakePlan plan = app::nextScheduledWake(now);
  rtc.planEpoch = plan.epoch;
  rtc.planKind = static_cast<uint8_t>(plan.kind);
}

[[noreturn]] void sleepUntilPlan() {
  const int64_t now = nowEpoch();
  int64_t seconds = rtc.planEpoch - now;
  if (seconds < 30) seconds = 30;
  Serial.printf("[power] deep sleep for %lld min\n",
                static_cast<long long>(seconds / 60));
  Serial.flush();

  connectivity.disconnect();
  // Keep the SHT30/battery gate (GPIO25) LOW through sleep.
  digitalWrite(board::kPeripheralEnable, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(board::kPeripheralEnable));
  gpio_deep_sleep_hold_en();

  // Wait for buttons to be released so a held key does not wake us at once.
  const uint32_t waitStart = millis();
  while ((digitalRead(board::kButtonUpper) == LOW ||
          digitalRead(board::kButtonMiddle) == LOW) &&
         millis() - waitStart < 5000) {
    delay(10);
  }

  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  // ESP32 ext1 cannot wake on "any low"; one pin with ALL_LOW is equivalent.
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(board::kButtonMiddle), 0);
  esp_sleep_enable_ext1_wakeup(1ULL << board::kButtonUpper,
                               ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
void requestRefresh(bool force) {
  refreshPending = true;
  forceRefresh = forceRefresh || force;
}

void setPage(app::Page next) {
  if (page != next) {
    page = next;
    requestRefresh(true);
  }
}

void interactiveFetch() {
  const bool ok = runFetch(FetchMode::Full);
  scheduleNext(!ok);
  requestRefresh(true);
}

void printInfo() {
  const app::UiModel model = buildModel();
  Serial.println("=== Epaper75 ===");
  Serial.printf("firmware=%s heap=%lu wake=%lu\n", board::kFirmwareVersion,
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(rtc.wakeCount));
  if (model.clockSynced) {
    const time_t local = static_cast<time_t>(model.nowEpoch + 8 * 3600);
    struct tm t;
    gmtime_r(&local, &t);
    Serial.printf("time=%04d-%02d-%02d %02d:%02d UTC+8\n", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
  }
  Serial.printf("qweather=%s location=%s wifi_saved=%s clock=%s\n",
                weatherSettings.hasCredentials ? "set" : "missing",
                weatherSettings.hasLocation ? "set" : "missing",
                connectivity.hasStoredCredentials() ? "yes" : "no",
                model.clockSynced ? "synced" : "unsynced");
  Serial.printf("weather=%s last_error=%s fetched_age_min=%lld alert=%s\n",
                rtc.weather.availability == app::DataAvailability::Available
                    ? "available"
                    : (rtc.weather.availability == app::DataAvailability::Unknown
                           ? "unknown"
                           : "unavailable"),
                app::weatherErrorName(rtc.weather.lastError),
                rtc.weather.fetchedEpoch > 0
                    ? static_cast<long long>((model.nowEpoch - rtc.weather.fetchedEpoch) / 60)
                    : -1LL,
                rtc.weather.alert.active ? "active" : "none");
  Serial.printf("page=%u panel=%s error=%s next_wake_min=%lu\n",
                static_cast<unsigned>(page),
                ui::panelJournalStateName(epd.journalState()),
                ui::epdErrorName(epd.lastError()),
                static_cast<unsigned long>(model.nextWakeMinutes));
  Serial.printf("battery=%u mV (~%d%%)\n", telemetry.batteryMillivolts,
                telemetry.batteryPercent);
}

void printHelp() {
  Serial.println("Commands (device sleeps after 90 s without activity):");
  Serial.println("  INFO                 redacted device state");
  Serial.println("  FETCH                fetch weather now and redraw");
  Serial.println("  REFRESH              force one full redraw");
  Serial.println("  PAGE 0|1             home or system page");
  Serial.println("  BATTERY              sample battery voltage");
  Serial.println("  QWEATHER <api-host> <developer-id> <project-id> <credential-id> <private-key-base64>");
  Serial.println("  QWEATHER CLEAR       erase stored QWeather settings");
  Serial.println("  LOCATION <lat> <lon> e.g. LOCATION 39.92 116.41");
  Serial.println("  WIFI SETUP           start the Epaper75-Setup portal");
  Serial.println("  SLEEP                sleep now");
}

char* trimInPlace(char* value) {
  while (*value == ' ' || *value == '\t') ++value;
  char* end = value + strlen(value);
  while (end > value && (end[-1] == ' ' || end[-1] == '\t')) --end;
  *end = '\0';
  return value;
}

void handleQWeather(char* arguments) {
  if (strcasecmp(arguments, "CLEAR") == 0) {
    settingsStore.clearWeather();
    settingsStore.loadWeather(weatherSettings);
    rtc.place = app::PlaceSnapshot();
    Serial.println("[qweather] settings cleared");
    requestRefresh(true);
    return;
  }
  char* save = nullptr;
  char* host = strtok_r(arguments, " \t", &save);
  char* developer = strtok_r(nullptr, " \t", &save);
  char* project = strtok_r(nullptr, " \t", &save);
  char* credential = strtok_r(nullptr, " \t", &save);
  char* key = strtok_r(nullptr, " \t", &save);
  char* extra = strtok_r(nullptr, " \t", &save);

  net::WeatherSettings next = weatherSettings;
  bool ok = host && developer && project && credential && key && !extra &&
            net::validateApiHost(host) && net::validateQWeatherId(developer) &&
            net::validateQWeatherId(project) &&
            net::validateQWeatherId(credential) &&
            net::parseEd25519PrivateKey(key, next.seed);
  if (key != nullptr) net::secureZero(key, strlen(key));
  if (ok) {
    app::copyText(next.apiHost, host);
    app::copyText(next.developerId, developer);
    app::copyText(next.projectId, project);
    app::copyText(next.credentialId, credential);
    ok = settingsStore.saveCredentials(next);
  }
  net::secureZero(next.seed, sizeof(next.seed));
  if (!ok) {
    Serial.println("[qweather] settings rejected");
    return;
  }
  settingsStore.loadWeather(weatherSettings);
  Serial.println("[qweather] settings stored");
}

void handleLocation(char* arguments) {
  char* save = nullptr;
  char* lat = strtok_r(arguments, " \t", &save);
  char* lon = strtok_r(nullptr, " \t", &save);
  int16_t latCenti = 0, lonCenti = 0;
  if (lat == nullptr || lon == nullptr || strtok_r(nullptr, " \t", &save) ||
      !net::parseCoordinate(lat, 9000, latCenti) ||
      !net::parseCoordinate(lon, 18000, lonCenti) ||
      !settingsStore.saveLocation(latCenti, lonCenti)) {
    Serial.println("[location] rejected");
    return;
  }
  settingsStore.loadWeather(weatherSettings);
  rtc.place = app::PlaceSnapshot();
  rtc.weather = app::WeatherSnapshot();
  Serial.println("[location] stored");
}

void handleSerialCommand(char* input) {
  char* command = trimInPlace(input);
  if (*command == '\0') return;
  lastActivityMs = millis();

  char* argument = strchr(command, ' ');
  if (argument != nullptr) {
    *argument++ = '\0';
    argument = trimInPlace(argument);
  }
  if (strcasecmp(command, "HELP") == 0) {
    printHelp();
  } else if (strcasecmp(command, "INFO") == 0) {
    printInfo();
  } else if (strcasecmp(command, "BATTERY") == 0) {
    sampleTelemetry();
    printInfo();
  } else if (strcasecmp(command, "FETCH") == 0) {
    interactiveFetch();
  } else if (strcasecmp(command, "REFRESH") == 0) {
    requestRefresh(true);
  } else if (strcasecmp(command, "SLEEP") == 0) {
    lastActivityMs = millis() - board::kInteractiveWindowMs;
  } else if (strcasecmp(command, "PAGE") == 0 && argument != nullptr &&
             (strcmp(argument, "0") == 0 || strcmp(argument, "1") == 0)) {
    setPage(argument[0] == '0' ? app::Page::Home : app::Page::System);
  } else if (strcasecmp(command, "QWEATHER") == 0 && argument != nullptr) {
    handleQWeather(argument);
  } else if (strcasecmp(command, "LOCATION") == 0 && argument != nullptr) {
    handleLocation(argument);
  } else if (strcasecmp(command, "WIFI") == 0 && argument != nullptr &&
             strcasecmp(argument, "SETUP") == 0) {
    connectivity.startPortal();
    Serial.printf("[wifi] portal %s\n", connectivity.portalActive() ? "started" : "failed");
  } else {
    Serial.println("Unknown command. Type HELP.");
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r' || value == '\n') {
      if (serialLength > 0) {
        serialLine[serialLength] = '\0';
        handleSerialCommand(serialLine);
        net::secureZero(serialLine, sizeof(serialLine));  // may hold a key
        serialLength = 0;
      }
      continue;
    }
    if (value >= 32 && value <= 126 && serialLength < sizeof(serialLine) - 1) {
      serialLine[serialLength++] = value;
    }
  }
}

void handleButton(board::ButtonId button, board::ButtonGesture gesture) {
  lastActivityMs = millis();
  Serial.printf("[button] %s %s\n", board::buttonName(button),
                board::buttonGestureName(gesture));
  const bool longPress = gesture == board::ButtonGesture::LongPress;
  switch (button) {
    case board::ButtonId::Upper:
      if (longPress) {
        interactiveFetch();
      } else {
        setPage(page == app::Page::Home ? app::Page::System : app::Page::Home);
      }
      break;
    case board::ButtonId::Middle:
      if (longPress) {
        setPage(app::Page::System);
      } else {
        interactiveFetch();
      }
      break;
    case board::ButtonId::Lower:
      setPage(app::Page::Home);
      break;
  }
}

// The button that woke us is consumed here (released or held long).
void handleWakeButton(board::ButtonId button, uint8_t pin) {
  const uint32_t start = millis();
  while (digitalRead(pin) == LOW && millis() - start < 3000) delay(10);
  const bool longPress = millis() - start >= board::kButtonLongPressMs;
  handleButton(button, longPress ? board::ButtonGesture::LongPress
                                 : board::ButtonGesture::ShortPress);
}

// ---------------------------------------------------------------------------
[[noreturn]] void runTimerWake() {
  const int64_t now = nowEpoch();
  if (clockSynced() && rtc.planEpoch - now > app::kEarlyWakeToleranceSeconds) {
    Serial.println("[power] early wake (RTC drift); sleeping again");
    sleepUntilPlan();
  }
  const app::WakeKind kind = static_cast<app::WakeKind>(rtc.planKind);
  Serial.printf("[wake] timer: %s\n",
                kind == app::WakeKind::AlertCheck ? "alert check" : "full update");
  bool ok = false;
  if (!batteryLow()) {
    ok = runFetch(kind == app::WakeKind::AlertCheck
                      ? FetchMode::AlertsThenFullIfChanged
                      : FetchMode::Full);
  }
  page = app::Page::Home;
  scheduleNext(!ok);
  updateDisplay(false);
  sleepUntilPlan();
}

}  // namespace

void setup() {
  gpio_hold_dis(static_cast<gpio_num_t>(board::kPeripheralEnable));
  gpio_deep_sleep_hold_dis();
  // ext0/ext1 wake leaves the button pins routed to the RTC domain; return
  // them to the digital GPIO matrix before Board configures them as inputs.
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::kButtonUpper));
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::kButtonMiddle));
  deviceBoard.begin();
  Serial.begin(board::kSerialBaud);
  delay(50);

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const bool fromSleep = cause == ESP_SLEEP_WAKEUP_TIMER ||
                         cause == ESP_SLEEP_WAKEUP_EXT0 ||
                         cause == ESP_SLEEP_WAKEUP_EXT1;
  if (!fromSleep || rtc.magic != kRtcMagic) {
    memset(rtcStorage, 0, sizeof(rtcStorage));
    new (rtcStorage) RtcState();
    rtc.magic = kRtcMagic;
  }
  ++rtc.wakeCount;
  setenv("TZ", board::kTimeZone, 1);
  tzset();

  Serial.println();
  Serial.printf("Epaper75 %s wake=%lu cause=%d reset=%d\n", board::kFirmwareVersion,
                static_cast<unsigned long>(rtc.wakeCount), static_cast<int>(cause),
                static_cast<int>(esp_reset_reason()));

  settingsStore.loadWeather(weatherSettings);
  if (!rtc.place.valid && weatherSettings.hasLocation) {
    net::PlaceCache cache;
    if (settingsStore.loadPlace(cache) &&
        cache.latitudeCenti == weatherSettings.latitudeCenti &&
        cache.longitudeCenti == weatherSettings.longitudeCenti) {
      rtc.place = cache.place;
    }
  }
  sampleTelemetry();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) runTimerWake();

  interactive = true;
  lastActivityMs = millis();
  page = app::Page::Home;
  deviceBoard.buttons().begin();
  printHelp();

  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    handleWakeButton(board::ButtonId::Middle, board::kButtonMiddle);
  } else if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    handleWakeButton(board::ButtonId::Upper, board::kButtonUpper);
  } else {
    // Cold boot: fetch immediately (or offer Wi-Fi setup) and redraw.
    if (!connectivity.hasStoredCredentials()) {
      Serial.println("[wifi] no stored network; starting Epaper75-Setup portal");
      connectivity.startPortal();
      scheduleNext(false);
      requestRefresh(true);
    } else {
      interactiveFetch();
    }
  }
  deviceBoard.buttons().clearEvents();
  printInfo();
}

void loop() {
  pollSerial();

  board::Buttons& buttons = deviceBoard.buttons();
  buttons.update(millis());
  board::ButtonEvent event;
  while (buttons.nextEvent(event)) handleButton(event.button, event.gesture);

  if (connectivity.portalActive()) {
    connectivity.loopPortal();
    lastActivityMs = millis();  // stay awake while the portal is open
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[wifi] provisioned");
      connectivity.stopPortal();
      interactiveFetch();
    }
  }

  if (refreshPending && millis() - lastRefreshEndMs >= board::kMinRefreshGapMs) {
    refreshPending = false;
    const bool force = forceRefresh;
    forceRefresh = false;
    if (!connectivity.portalActive()) {
      updateDisplay(force);
    } else {
      // Show setup instructions without leaving the portal.
      updateDisplay(true);
    }
    lastActivityMs = millis();
  }

  if (!connectivity.portalActive() && !refreshPending &&
      millis() - lastActivityMs >= board::kInteractiveWindowMs) {
    if (page != app::Page::Home) {
      page = app::Page::Home;
      updateDisplay(false);
    }
    if (rtc.planEpoch == 0 || (clockSynced() && rtc.planEpoch <= nowEpoch())) {
      scheduleNext(false);
    }
    sleepUntilPlan();
  }
  delay(5);
}
