#include "net/NetworkProbe.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <limits.h>
#include <string.h>

namespace net {
namespace {

constexpr uint16_t kProbePort = 443;
constexpr uint32_t kConnectTimeoutMs = 1200;
constexpr uint32_t kInitialTargetSpacingMs = 250;
constexpr uint32_t kTargetSpacingMs = 15000;

struct TargetDefinition {
  const char* name;
  const char* host;
};

constexpr TargetDefinition kTargets[kNetworkProbeTargetCount] = {
    {"NetEase", "music.163.com"},
    {"GitHub", "github.com"},
    {"Google", "google.com"},
    {"YouTube", "youtube.com"},
};

static_assert(sizeof(kTargets) / sizeof(kTargets[0]) ==
                  kNetworkProbeTargetCount,
              "Network probe target count mismatch");

}  // namespace

void NetworkProbe::begin() {
  portENTER_CRITICAL(&mux_);
  snapshot_ = NetworkProbeSnapshot{};
  for (size_t i = 0; i < kNetworkProbeTargetCount; ++i) {
    strlcpy(snapshot_.targets[i].name, kTargets[i].name,
            sizeof(snapshot_.targets[i].name));
    strlcpy(snapshot_.targets[i].host, kTargets[i].host,
            sizeof(snapshot_.targets[i].host));
    snapshot_.targets[i].port = kProbePort;
  }
  portEXIT_CRITICAL(&mux_);

  begun_ = true;
  wifiWasConnected_ = false;
  offlinePublished_ = false;
  fastInitialRound_ = true;
  nextTargetIndex_ = 0;
  nextProbeAtMs_ = millis();
}

void NetworkProbe::loop() {
  if (!begun_) return;

  const uint32_t now = millis();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    wifiWasConnected_ = false;
    if (!offlinePublished_) {
      markWifiDisconnected(now);
      offlinePublished_ = true;
    }
    return;
  }

  if (!wifiWasConnected_) {
    // Start a fresh, ordered round immediately after Wi-Fi reconnects.
    wifiWasConnected_ = true;
    offlinePublished_ = false;
    fastInitialRound_ = true;
    nextTargetIndex_ = 0;
    nextProbeAtMs_ = now;
  }

  if (static_cast<int32_t>(now - nextProbeAtMs_) < 0) return;

  const size_t targetIndex = nextTargetIndex_;
  probeTarget(targetIndex);

  nextTargetIndex_ = targetIndex + 1;
  if (nextTargetIndex_ >= kNetworkProbeTargetCount) {
    nextTargetIndex_ = 0;
    portENTER_CRITICAL(&mux_);
    ++snapshot_.completedRounds;
    portEXIT_CRITICAL(&mux_);
    fastInitialRound_ = false;
  }

  // Schedule from completion, not the prior deadline. If another subsystem
  // blocks the main loop (for example a full e-paper refresh), this prevents
  // several overdue 1.2-second attempts from running back-to-back.
  nextProbeAtMs_ =
      millis() +
      (fastInitialRound_ ? kInitialTargetSpacingMs : kTargetSpacingMs);
}

NetworkProbeSnapshot NetworkProbe::snapshot() const {
  portENTER_CRITICAL(&mux_);
  const NetworkProbeSnapshot copy = snapshot_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

void NetworkProbe::markWifiDisconnected(uint32_t sampledAtMs) {
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < kNetworkProbeTargetCount; ++i) {
    snapshot_.targets[i].reachable = false;
    snapshot_.targets[i].latencyMs = kNetworkProbeUnavailableMs;
    snapshot_.targets[i].sampledAtMs = sampledAtMs;
  }
  snapshot_.updatedAtMs = sampledAtMs;
  portEXIT_CRITICAL(&mux_);
}

void NetworkProbe::probeTarget(size_t targetIndex) {
  if (targetIndex >= kNetworkProbeTargetCount) return;

  WiFiClient client;
  const uint32_t startedAtMs = millis();
  // WiFiClient is intentionally used instead of WiFiClientSecure: this
  // measures DNS + TCP establishment only and performs no TLS handshake.
  const bool connected =
      client.connect(kTargets[targetIndex].host, kProbePort,
                     static_cast<int32_t>(kConnectTimeoutMs));
  const uint32_t finishedAtMs = millis();
  client.stop();

  const uint32_t elapsedMs = finishedAtMs - startedAtMs;
  const int32_t latencyMs =
      connected
          ? static_cast<int32_t>(elapsedMs > static_cast<uint32_t>(INT32_MAX)
                                     ? INT32_MAX
                                     : elapsedMs)
          : kNetworkProbeUnavailableMs;

  portENTER_CRITICAL(&mux_);
  NetworkProbeTargetSnapshot& target = snapshot_.targets[targetIndex];
  target.reachable = connected;
  target.latencyMs = latencyMs;
  target.sampledAtMs = finishedAtMs;
  snapshot_.updatedAtMs = finishedAtMs;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace net
