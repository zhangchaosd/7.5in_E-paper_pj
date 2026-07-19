#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <esp_system.h>

#include "app/RefreshPolicy.h"
#include "app/RuntimeState.h"
#include "board/Board.h"
#include "board/BoardConfig.h"
#include "net/AgentClient.h"
#include "net/Connectivity.h"
#include "net/DeviceServer.h"
#include "net/SettingsStore.h"
#include "ui/EpdController.h"

namespace {

app::RuntimeState runtimeState;
board::Board deviceBoard;
net::SettingsStore settingsStore;
net::Connectivity connectivity(runtimeState);
net::AgentClient agentClient(runtimeState, settingsStore,
                             board::kAgentPollSlotPeriodMs);
net::DeviceServer deviceServer(runtimeState, board::kFirmwareVersion);
ui::EpdController epd;
app::RefreshPolicy refreshPolicy;

char serialLine[320] = {};
size_t serialLength = 0;
bool serverStarted = false;
uint32_t lastServerAttemptAtMs = 0;
uint32_t lastAgentTaskAttemptAtMs = 0;
uint32_t lastSensorSampleAtMs = 0;
// Give stored Wi-Fi credentials and the first Agent poll a chance to settle
// before committing a 20+ second physical refresh.  If no Wi-Fi is available,
// the system page is drawn just before the provisioning portal starts.
constexpr uint32_t kInitialDisplayGraceMs = 10000;

void publishTelemetry() {
  const board::TelemetryReading reading = deviceBoard.sampleTelemetry();
  app::TelemetrySnapshot snapshot;
  snapshot.sensorOk = reading.environment.ok;
  snapshot.temperatureC = reading.environment.temperatureC;
  snapshot.humidityPercent = reading.environment.humidityPercent;
  snapshot.batteryMillivolts = reading.battery.batteryMillivolts;
  snapshot.batteryPercent = reading.battery.batteryPercent;
  snapshot.sampledAtMs = reading.sampledAtMs;
  runtimeState.updateTelemetry(snapshot);
  lastSensorSampleAtMs = reading.sampledAtMs;

  if (reading.environment.ok) {
    Serial.printf("[sensor] %.2f C, %.2f %%RH, battery %u mV (~%d%%)\n",
                  reading.environment.temperatureC,
                  reading.environment.humidityPercent,
                  reading.battery.batteryMillivolts,
                  reading.battery.batteryPercent);
  } else {
    Serial.printf("[sensor] SHT30 %s, battery %u mV (~%d%%)\n",
                  board::sht30ErrorName(reading.environment.error),
                  reading.battery.batteryMillivolts,
                  reading.battery.batteryPercent);
  }
}

void printInfo() {
  const app::UiModel model = runtimeState.snapshot();
  Serial.println("=== Epaper75 P1 ===");
  Serial.printf("firmware=%s heap=%lu flash=%lu psram=%lu\n",
                board::kFirmwareVersion,
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("wifi=%s rssi=%d agents=%u/%u action=%u account=%s%s\n",
                model.network.connected ? "connected" : "offline",
                model.network.rssi,
                static_cast<unsigned>(app::fleetOnlineCount(model.fleet)),
                static_cast<unsigned>(app::kAgentDeviceCount),
                static_cast<unsigned>(app::fleetActionCount(model.fleet)),
                app::dataAvailabilityName(model.fleet.account.availability),
                model.fleet.account.stale ? " stale" : "");
  Serial.printf("mode=%s page=%u model_rev=%lu panel=%s error=%s\n",
                app::displayModeName(model.mode), model.page,
                static_cast<unsigned long>(model.revision),
                ui::panelJournalStateName(epd.journalState()),
                 ui::epdErrorName(epd.lastError()));
  if (model.telemetry.batteryPercent >= 0) {
    Serial.printf("battery=%u mV (~%d%%)\n",
                  model.telemetry.batteryMillivolts,
                  model.telemetry.batteryPercent);
  }
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  INFO                  cached device state");
  Serial.println("  SENSOR                sample SHT30 and battery now");
  Serial.println("  POLL                  request an immediate Agent poll");
  Serial.println("  AGENTS ...            write private Agent settings over USB");
  Serial.println("  REFRESH               request a rate-limited full refresh");
  Serial.println("  PAGE 0|1              dashboard or system page");
  Serial.println("  MODE auto|claude|codex");
  Serial.println("  HELP");
}

char* trimInPlace(char* value) {
  while (*value == ' ' || *value == '\t') ++value;
  char* end = value + strlen(value);
  while (end > value && (end[-1] == ' ' || end[-1] == '\t')) --end;
  *end = '\0';
  return value;
}

void handleSerialCommand(char* input) {
  char* command = trimInPlace(input);
  if (*command == '\0') return;

  if (strcasecmp(command, "HELP") == 0) {
    printHelp();
    return;
  }
  if (strcasecmp(command, "INFO") == 0) {
    printInfo();
    return;
  }
  if (strcasecmp(command, "SENSOR") == 0) {
    publishTelemetry();
    return;
  }
  if (strcasecmp(command, "POLL") == 0) {
    agentClient.requestImmediatePoll();
    Serial.println("[agents] immediate poll requested");
    return;
  }
  if (strcasecmp(command, "REFRESH") == 0) {
    runtimeState.requestRefresh();
    Serial.println("[display] refresh queued (panel rate limit still applies)");
    return;
  }

  char* argument = strchr(command, ' ');
  if (argument == nullptr) {
    Serial.println("Unknown command. Type HELP.");
    return;
  }
  *argument++ = '\0';
  argument = trimInPlace(argument);

  if (strcasecmp(command, "PAGE") == 0) {
    if (strcmp(argument, "0") != 0 && strcmp(argument, "1") != 0) {
      Serial.println("PAGE must be 0 or 1.");
      return;
    }
    runtimeState.setPage(static_cast<uint8_t>(argument[0] - '0'));
    runtimeState.requestRefresh();
    return;
  }

  if (strcasecmp(command, "AGENTS") == 0) {
    net::AgentFleetSettings agents;
    char* save = nullptr;
    bool valid = true;
    for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
      char* endpoint = strtok_r(i == 0 ? argument : nullptr, " \t", &save);
      if (endpoint == nullptr ||
          !net::parseAgentEndpoint(endpoint, agents.endpoints[i])) {
        valid = false;
        break;
      }
    }
    char* token = valid ? strtok_r(nullptr, " \t", &save) : nullptr;
    char* extra = token != nullptr ? strtok_r(nullptr, " \t", &save) : nullptr;
    if (token == nullptr || extra != nullptr ||
        !net::validateAgentToken(token)) {
      valid = false;
    }
    if (valid) {
      strlcpy(agents.token, token, sizeof(agents.token));
      agents.primaryIndex = 0;
      valid = settingsStore.saveAgents(agents);
      memset(agents.token, 0, sizeof(agents.token));
    }
    if (!valid) {
      Serial.println("Agent settings rejected.");
      return;
    }
    agentClient.requestImmediatePoll();
    runtimeState.setPage(0);
    runtimeState.requestRefresh();
    Serial.println("[agents] private settings stored");
    return;
  }

  if (strcasecmp(command, "MODE") == 0) {
    app::DisplayMode mode;
    if (!app::parseDisplayMode(argument, mode)) {
      Serial.println("MODE must be auto, claude or codex.");
      return;
    }
    if (!settingsStore.saveDisplayMode(mode)) {
      Serial.println("Failed to persist display mode.");
      return;
    }
    runtimeState.setDisplayMode(mode);
    runtimeState.requestRefresh();
    Serial.printf("mode=%s\n", app::displayModeName(mode));
    return;
  }

  Serial.println("Unknown command. Type HELP.");
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r' || value == '\n') {
      if (serialLength > 0) {
        serialLine[serialLength] = '\0';
        handleSerialCommand(serialLine);
        memset(serialLine, 0, sizeof(serialLine));
        serialLength = 0;
      }
      continue;
    }
    if (value >= 32 && value <= 126 &&
        serialLength < sizeof(serialLine) - 1) {
      serialLine[serialLength++] = value;
    }
  }
}

void handleButtonEvent(const board::ButtonEvent& event) {
  Serial.printf("[button] %s %s\n", board::buttonName(event.button),
                board::buttonGestureName(event.gesture));
  const app::UiModel model = runtimeState.snapshot();

  if (event.gesture == board::ButtonGesture::LongPress) {
    switch (event.button) {
      case board::ButtonId::Upper:
        agentClient.requestImmediatePoll();
        break;
      case board::ButtonId::Middle:
        if (runtimeState.setPage(1)) runtimeState.requestRefresh();
        break;
      case board::ButtonId::Lower:
        if (runtimeState.setPage(0)) runtimeState.requestRefresh();
        break;
    }
    return;
  }

  switch (event.button) {
    case board::ButtonId::Upper:
      if (runtimeState.setPage(static_cast<uint8_t>((model.page + 1) % 2))) {
        runtimeState.requestRefresh();
      }
      break;
    case board::ButtonId::Middle:
      runtimeState.requestRefresh();
      break;
    case board::ButtonId::Lower:
      if (model.page > 0 && runtimeState.setPage(model.page - 1)) {
        runtimeState.requestRefresh();
      }
      break;
  }
}

void pollButtons() {
  board::Buttons& buttons = deviceBoard.buttons();
  buttons.update(millis());
  board::ButtonEvent event;
  while (buttons.nextEvent(event)) handleButtonEvent(event);
}

void ensureDeviceServer(uint32_t nowMs) {
  if (connectivity.portalActive()) {
    if (serverStarted) {
      deviceServer.end();
      serverStarted = false;
      Serial.println("[http] stopped while provisioning portal owns port 80");
    }
    return;
  }
  if (serverStarted || !connectivity.snapshot().connected) {
    return;
  }
  if (lastServerAttemptAtMs != 0 &&
      nowMs - lastServerAttemptAtMs < 5000) {
    return;
  }
  lastServerAttemptAtMs = nowMs;
  serverStarted = deviceServer.begin();
  Serial.printf("[http] device API %s\n",
                serverStarted ? "started on port 80" : "start failed");
}

void serviceDisplay(uint32_t nowMs) {
  if (runtimeState.consumeRefreshRequest()) refreshPolicy.force(nowMs);

  const app::UiModel observed = runtimeState.snapshot();
  refreshPolicy.observe(observed, nowMs);
  if (refreshPolicy.lastRefreshAtMs() == 0 && nowMs < kInitialDisplayGraceMs) {
    return;
  }
  if (!refreshPolicy.due(nowMs) || connectivity.portalActive() ||
      epd.isFaultLocked()) {
    return;
  }

  Serial.printf("[display] refresh revision %lu page %u starting\n",
                static_cast<unsigned long>(observed.revision), observed.page);
  const bool ok = epd.refresh(observed);
  const uint32_t finishedAtMs = millis();
  if (ok) {
    runtimeState.markDisplayed(finishedAtMs);
    refreshPolicy.markCompleted(observed.revision, finishedAtMs);
    Serial.printf("[display] completed in %lu ms\n",
                  static_cast<unsigned long>(finishedAtMs - nowMs));
  } else {
    Serial.printf("[display] failed: %s (journal=%s)\n",
                  ui::epdErrorName(epd.lastError()),
                  ui::panelJournalStateName(epd.journalState()));
    if (epd.isFaultLocked()) {
      // A fault-locked panel must never be placed in an automatic retry loop.
      refreshPolicy.markCompleted(observed.revision, finishedAtMs);
    }
  }
}

}  // namespace

void setup() {
  deviceBoard.begin();
  Serial.begin(board::kSerialBaud);
  delay(350);

  Serial.println();
  Serial.println("Epaper75 P1 starting");
  Serial.printf("chip=%s rev=%d cores=%d flash=%lu psram=%lu reset=%d\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<int>(esp_reset_reason()));

  net::StoredSettings stored;
  settingsStore.load(stored);
  runtimeState.setDisplayMode(stored.mode);
  if (!stored.agents.ready()) runtimeState.setPage(1);

  publishTelemetry();
  connectivity.begin();
  runtimeState.updateNetwork(connectivity.snapshot());
  lastAgentTaskAttemptAtMs = millis();
  Serial.printf("[agents] polling task %s\n",
                agentClient.begin() ? "started on core 0" : "start failed");
  printHelp();
  printInfo();
}

void loop() {
  const uint32_t nowMs = millis();

  pollSerial();
  pollButtons();
  connectivity.loop();
  runtimeState.updateNetwork(connectivity.snapshot());
  ensureDeviceServer(nowMs);
  if (!agentClient.running() &&
      nowMs - lastAgentTaskAttemptAtMs >= 10000U) {
    lastAgentTaskAttemptAtMs = nowMs;
    Serial.printf("[agents] polling task retry %s\n",
                  agentClient.begin() ? "accepted" : "failed");
  }
  if (nowMs - lastSensorSampleAtMs >= board::kSensorPeriodMs) {
    publishTelemetry();
  }

  serviceDisplay(nowMs);
  delay(2);
}
