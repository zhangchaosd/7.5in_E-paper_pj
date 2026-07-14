#include "net/DeviceServer.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>

namespace net {
namespace {

constexpr uint32_t kServerStackBytes = 8192;
constexpr UBaseType_t kServerPriority = 2;
constexpr BaseType_t kServerCore = 0;

String endpointText(const app::BridgeEndpoint& endpoint) {
  if (!endpoint.configured()) return String();
  String value;
  value.reserve(strlen(endpoint.host) + 7);
  value += endpoint.host;
  value += ':';
  value += endpoint.port;
  return value;
}

}  // namespace

DeviceServer::DeviceServer(app::RuntimeState& state, SettingsStore& settings,
                           const char* firmwareVersion)
    : state_(state), settings_(settings) {
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
  server_.on("/api/bridge", HTTP_POST, [this]() { handleBridge(); });
  server_.on("/api/display", HTTP_POST, [this]() { handleDisplay(); });
  server_.on("/api/refresh", HTTP_POST, [this]() { handleRefresh(); });
  server_.onNotFound([this]() { handleNotFound(); });
  routesConfigured_ = true;
}

void DeviceServer::handleInfo() {
  const app::UiModel model = state_.snapshot();
  JsonDocument document;
  document["device_type"] = "epaper75";
  document["api_version"] = 1;
  document["fw"] = firmwareVersion_;
  document["ip"] = model.network.ip;
  document["ssid"] = model.network.ssid;
  document["bridge"] = endpointText(model.endpoint);
  document["mode"] = app::displayModeName(model.mode);
  // Keep the legacy mode field for Bridge compatibility. P1 v0.2 always
  // renders Codex usage plus network latency on page 0.
  document["effective"] = "codex";
  document["showing"] = (model.page % 2U) == 0U ? "codex_network" : "system";
  document["last_update_s"] =
      model.bridge.receivedAtMs == 0
          ? -1
          : static_cast<int32_t>((millis() - model.bridge.receivedAtMs) / 1000);
  document["sprite_rev"] = 0;
  document["brightness"] = 100;
  document["display_width"] = 800;
  document["display_height"] = 480;
  document["revision"] = model.revision;

  JsonObject battery = document["battery"].to<JsonObject>();
  battery["estimated"] = true;
  battery["millivolts"] = model.telemetry.batteryMillivolts;
  if (model.telemetry.batteryPercent >= 0) {
    battery["percent"] = model.telemetry.batteryPercent;
  } else {
    battery["percent"] = nullptr;
  }

  JsonArray latency = document["network_latency"].to<JsonArray>();
  for (size_t i = 0; i < app::kLatencyTargetCount; ++i) {
    const app::LatencyTargetSnapshot& target =
        model.networkQuality.targets[i];
    JsonObject item = latency.add<JsonObject>();
    item["name"] = target.name;
    item["reachable"] = target.sampledAtMs != 0 && target.reachable;
    if (target.sampledAtMs != 0 && target.reachable && target.latencyMs >= 0) {
      item["latency_ms"] = target.latencyMs;
    } else {
      item["latency_ms"] = nullptr;
    }
    item["sampled"] = target.sampledAtMs != 0;
  }
  document["network_probe"] = "dns_tcp_443";
  document["network_probe_rounds"] = model.networkQuality.completedRounds;

  JsonObject claude = document["claude"].to<JsonObject>();
  claude["status"] = model.bridge.claude.status;
  claude["needs_input"] = model.bridge.claude.needsInput;
  claude["custom_sprite"] = false;
  claude["w"] = 0;
  claude["h"] = 0;

  JsonObject codex = document["codex"].to<JsonObject>();
  codex["status"] = model.bridge.codex.status;
  codex["needs_input"] = model.bridge.codex.needsInput;
  codex["custom_sprite"] = false;
  codex["w"] = 0;
  codex["h"] = 0;

  String output;
  output.reserve(1400);
  serializeJson(document, output);
  sendJson(200, output);
}

void DeviceServer::handleHealth() {
  const app::UiModel model = state_.snapshot();
  JsonDocument document;
  document["ok"] = true;
  document["uptime_ms"] = millis();
  document["wifi_connected"] = model.network.connected;
  document["bridge_configured"] = model.endpoint.configured();
  document["bridge_reachable"] = model.bridge.reachable;
  document["battery_percent_estimate"] =
      model.telemetry.batteryPercent >= 0
          ? static_cast<int>(model.telemetry.batteryPercent)
          : -1;
  document["network_probe_rounds"] = model.networkQuality.completedRounds;
  document["free_heap"] = ESP.getFreeHeap();
  String output;
  output.reserve(192);
  serializeJson(document, output);
  sendJson(200, output);
}

void DeviceServer::handleBridge() {
  if (!server_.hasArg("host")) {
    sendError(400, "missing host");
    return;
  }

  app::BridgeEndpoint endpoint;
  if (!parseBridgeEndpoint(server_.arg("host").c_str(), endpoint)) {
    sendError(400, "host must be ip_or_name[:port]");
    return;
  }
  if (!settings_.saveBridge(endpoint)) {
    sendError(500, "failed to persist bridge");
    return;
  }

  state_.setBridgeEndpoint(endpoint);
  app::BridgeSnapshot unavailable = state_.snapshot().bridge;
  unavailable.reachable = false;
  state_.updateBridge(unavailable);
  state_.requestRefresh();
  sendJson(200, "{\"ok\":true}");
}

void DeviceServer::handleDisplay() {
  if (!server_.hasArg("mode")) {
    sendError(400, "missing mode");
    return;
  }

  app::DisplayMode mode;
  if (!app::parseDisplayMode(server_.arg("mode").c_str(), mode)) {
    sendError(400, "mode must be auto|claude|codex");
    return;
  }
  if (!settings_.saveDisplayMode(mode)) {
    sendError(500, "failed to persist display mode");
    return;
  }

  state_.setDisplayMode(mode);
  state_.requestRefresh();
  sendJson(200, "{\"ok\":true}");
}

void DeviceServer::handleRefresh() {
  state_.requestRefresh();
  sendJson(200, "{\"ok\":true,\"queued\":true}");
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
