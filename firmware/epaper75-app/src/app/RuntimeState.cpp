#include "app/RuntimeState.h"

#include <math.h>
#include <string.h>

namespace app {
namespace {

bool batteryTelemetryEqual(const TelemetrySnapshot& lhs,
                           const TelemetrySnapshot& rhs) {
  const int batteryDelta = static_cast<int>(lhs.batteryMillivolts) -
                           static_cast<int>(rhs.batteryMillivolts);
  const int batteryPercentDelta = static_cast<int>(lhs.batteryPercent) -
                                  static_cast<int>(rhs.batteryPercent);
  const bool batteryPercentEqual =
      lhs.batteryPercent == rhs.batteryPercent ||
      (lhs.batteryPercent >= 0 && rhs.batteryPercent >= 0 &&
       abs(batteryPercentDelta) < 2);
  return abs(batteryDelta) < 20 && batteryPercentEqual;
}

bool telemetryEqual(const TelemetrySnapshot& lhs,
                    const TelemetrySnapshot& rhs) {
  const bool temperaturesEqual =
      (isnan(lhs.temperatureC) && isnan(rhs.temperatureC)) ||
      fabsf(lhs.temperatureC - rhs.temperatureC) < 0.2f;
  const bool humidityEqual =
      (isnan(lhs.humidityPercent) && isnan(rhs.humidityPercent)) ||
      fabsf(lhs.humidityPercent - rhs.humidityPercent) < 1.0f;
  return lhs.sensorOk == rhs.sensorOk && temperaturesEqual && humidityEqual &&
         batteryTelemetryEqual(lhs, rhs);
}

bool networkQualityEqual(const NetworkQualitySnapshot& lhs,
                         const NetworkQualitySnapshot& rhs) {
  if ((lhs.completedRounds == 0) != (rhs.completedRounds == 0)) return false;
  // E-paper cannot sensibly redraw for every millisecond of Wi-Fi jitter.
  // Force one exact-value redraw per five completed rounds (about five
  // minutes), while quality-band and reachability transitions remain prompt.
  if (lhs.completedRounds / 5U != rhs.completedRounds / 5U) return false;
  for (size_t i = 0; i < kLatencyTargetCount; ++i) {
    const LatencyTargetSnapshot& left = lhs.targets[i];
    const LatencyTargetSnapshot& right = rhs.targets[i];
    if (strcmp(left.name, right.name) != 0 ||
        (left.sampledAtMs == 0) != (right.sampledAtMs == 0) ||
        left.reachable != right.reachable) {
      return false;
    }
    if (left.reachable) {
      const uint8_t leftBand = left.latencyMs <= 100
                                   ? 0
                                   : (left.latencyMs <= 300
                                          ? 1
                                          : (left.latencyMs <= 700 ? 2 : 3));
      const uint8_t rightBand = right.latencyMs <= 100
                                    ? 0
                                    : (right.latencyMs <= 300
                                           ? 1
                                           : (right.latencyMs <= 700 ? 2 : 3));
      if (leftBand != rightBand) return false;
    }
  }
  return true;
}

}  // namespace

RuntimeState::RuntimeState() = default;

UiModel RuntimeState::snapshot() const {
  portENTER_CRITICAL(&mux_);
  const UiModel copy = model_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

BridgeEndpoint RuntimeState::bridgeEndpoint() const {
  portENTER_CRITICAL(&mux_);
  const BridgeEndpoint copy = model_.endpoint;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

void RuntimeState::bumpRevisionLocked() {
  ++model_.revision;
  if (model_.revision == 0) model_.revision = 1;
}

bool RuntimeState::updateNetwork(const NetworkSnapshot& value) {
  portENTER_CRITICAL(&mux_);
  const bool changed = model_.network.connected != value.connected ||
                       strcmp(model_.network.ssid, value.ssid) != 0 ||
                       strcmp(model_.network.ip, value.ip) != 0;
  model_.network = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

bool RuntimeState::updateNetworkQuality(const NetworkQualitySnapshot& value) {
  portENTER_CRITICAL(&mux_);
  const bool changed =
      !networkQualityEqual(model_.networkQuality, value);
  model_.networkQuality = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

bool RuntimeState::updateTelemetry(const TelemetrySnapshot& value) {
  portENTER_CRITICAL(&mux_);
  // Temperature and humidity are intentionally absent from page 0. Do not
  // wear the panel for hidden sensor-only changes; switching to page 1 bumps
  // the revision and renders the newest cached values there.
  const bool changed = (model_.page % 2U) == 0U
                           ? !batteryTelemetryEqual(model_.telemetry, value)
                           : !telemetryEqual(model_.telemetry, value);
  model_.telemetry = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

bool RuntimeState::updateBridge(const BridgeSnapshot& value) {
  portENTER_CRITICAL(&mux_);
  const bool changed = !bridgeSemanticallyEqual(model_.bridge, value);
  model_.bridge = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

bool RuntimeState::setBridgeEndpoint(const BridgeEndpoint& value) {
  portENTER_CRITICAL(&mux_);
  const bool changed = model_.endpoint.port != value.port ||
                       strcmp(model_.endpoint.host, value.host) != 0;
  model_.endpoint = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

bool RuntimeState::setDisplayMode(DisplayMode value) {
  portENTER_CRITICAL(&mux_);
  const bool changed = model_.mode != value;
  model_.mode = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

bool RuntimeState::setPage(uint8_t value) {
  portENTER_CRITICAL(&mux_);
  const bool changed = model_.page != value;
  model_.page = value;
  if (changed) bumpRevisionLocked();
  portEXIT_CRITICAL(&mux_);
  return changed;
}

void RuntimeState::requestRefresh() {
  portENTER_CRITICAL(&mux_);
  forceRefresh_ = true;
  portEXIT_CRITICAL(&mux_);
}

bool RuntimeState::consumeRefreshRequest() {
  portENTER_CRITICAL(&mux_);
  const bool requested = forceRefresh_;
  forceRefresh_ = false;
  portEXIT_CRITICAL(&mux_);
  return requested;
}

void RuntimeState::markDisplayed(uint32_t atMs) {
  portENTER_CRITICAL(&mux_);
  model_.lastDisplayAtMs = atMs;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace app
