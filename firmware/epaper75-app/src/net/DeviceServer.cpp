#include "net/DeviceServer.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>

namespace net {
namespace {

constexpr uint32_t kServerStackBytes = 8192;
constexpr UBaseType_t kServerPriority = 2;
constexpr BaseType_t kServerCore = 0;

uint8_t fleetConfiguredCount(const app::AgentFleetSnapshot& fleet) {
  uint8_t count = 0;
  for (const app::AgentDeviceSnapshot& device : fleet.devices) {
    if (device.reachability != app::AgentReachability::Unconfigured) ++count;
  }
  return count;
}

void addAccountAggregate(JsonDocument& document,
                         const app::AccountQuotaSnapshot& account) {
  JsonObject output = document["account"].to<JsonObject>();
  output["availability"] = app::dataAvailabilityName(account.availability);
  output["stale"] = account.stale;
  const int16_t weeklyRemaining =
      app::accountWeeklyRemainingPercent(account);
  if (weeklyRemaining >= 0) {
    output["weekly_remaining_percent"] = weeklyRemaining;
  } else {
    output["weekly_remaining_percent"] = nullptr;
  }
  if (account.weeklyResetTimestamp > 0) {
    output["weekly_reset_at"] = account.weeklyResetTimestamp;
  } else {
    output["weekly_reset_at"] = nullptr;
  }
  if (account.receivedAtMs != 0) {
    output["updated_age_s"] =
        static_cast<uint32_t>(millis() - account.receivedAtMs) / 1000U;
  } else {
    output["updated_age_s"] = nullptr;
  }
}

}  // namespace

DeviceServer::DeviceServer(app::RuntimeState& state,
                           const char* firmwareVersion)
    : state_(state) {
  strlcpy(firmwareVersion_, firmwareVersion == nullptr ? "p1" : firmwareVersion,
          sizeof(firmwareVersion_));
}

DeviceServer::~DeviceServer() { end(); }

bool DeviceServer::begin() {
  if (taskHandle_ != nullptr) return true;
  configureRoutes();
  stopRequested_ = false;
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "ep75-http", kServerStackBytes, this, kServerPriority,
      &taskHandle_, kServerCore);
  if (created != pdPASS) {
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

void DeviceServer::end() {
  TaskHandle_t task = taskHandle_;
  if (task == nullptr) return;
  stopRequested_ = true;
  const uint32_t startedAt = millis();
  while (taskHandle_ != nullptr && millis() - startedAt < 1000) {
    delay(10);
  }
  if (taskHandle_ != nullptr) {
    vTaskDelete(taskHandle_);
    taskHandle_ = nullptr;
    running_ = false;
  }
}

bool DeviceServer::running() const { return running_; }

void DeviceServer::taskEntry(void* context) {
  static_cast<DeviceServer*>(context)->run();
}

void DeviceServer::run() {
  // WiFiManager owns port 80 while its setup AP is active. Waiting for a STA
  // connection prevents the two web servers from competing for the port.
  while (!stopRequested_ && WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (!stopRequested_) vTaskDelay(pdMS_TO_TICKS(300));

  if (!stopRequested_) {
    server_.begin();
    running_ = true;
  }
  while (!stopRequested_ && running_) {
    server_.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  if (running_) server_.stop();
  running_ = false;
  taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void DeviceServer::configureRoutes() {
  if (routesConfigured_) return;
  server_.on("/api/info", HTTP_GET, [this]() { handleInfo(); });
  server_.on("/api/health", HTTP_GET, [this]() { handleHealth(); });
  server_.onNotFound([this]() { handleNotFound(); });
  routesConfigured_ = true;
}

void DeviceServer::handleInfo() {
  const app::UiModel model = state_.snapshot();
  JsonDocument document;
  document["device_type"] = "epaper75";
  document["api_version"] = 2;
  document["fw"] = firmwareVersion_;
  document["wifi_connected"] = model.network.connected;
  document["mode"] = app::displayModeName(model.mode);
  document["showing"] = (model.page % 2U) == 0U ? "agent_fleet" : "system";
  document["display_width"] = 800;
  document["display_height"] = 480;
  document["revision"] = model.revision;
  document["agents_configured"] = fleetConfiguredCount(model.fleet);
  document["agents_online"] = app::fleetOnlineCount(model.fleet);
  document["agents_action_required"] = app::fleetActionCount(model.fleet);
  document["agents_error"] = app::fleetErrorCount(model.fleet);
  addAccountAggregate(document, model.fleet.account);

  String output;
  output.reserve(512);
  serializeJson(document, output);
  sendJson(200, output);
}

void DeviceServer::handleHealth() {
  const app::UiModel model = state_.snapshot();
  JsonDocument document;
  document["ok"] = true;
  document["uptime_ms"] = millis();
  document["wifi_connected"] = model.network.connected;
  document["agents_configured"] = fleetConfiguredCount(model.fleet);
  document["agents_online"] = app::fleetOnlineCount(model.fleet);
  document["agents_action_required"] = app::fleetActionCount(model.fleet);
  document["agents_error"] = app::fleetErrorCount(model.fleet);
  document["account_availability"] =
      app::dataAvailabilityName(model.fleet.account.availability);
  document["free_heap"] = ESP.getFreeHeap();
  String output;
  output.reserve(256);
  serializeJson(document, output);
  sendJson(200, output);
}

void DeviceServer::handleNotFound() { sendError(404, "not found"); }

void DeviceServer::sendJson(int statusCode, const String& body) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(statusCode, "application/json; charset=utf-8", body);
}

void DeviceServer::sendError(int statusCode, const char* message) {
  JsonDocument document;
  document["ok"] = false;
  document["error"] = message;
  String output;
  serializeJson(document, output);
  sendJson(statusCode, output);
}

}  // namespace net
