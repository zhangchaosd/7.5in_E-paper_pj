#pragma once

#include <Arduino.h>

namespace net {

constexpr size_t kNetworkProbeTargetCount = 4;
constexpr size_t kNetworkProbeNameSize = 12;
constexpr size_t kNetworkProbeHostSize = 32;
constexpr int32_t kNetworkProbeUnavailableMs = -1;

// Fixed-size, trivially copyable data so callers do not retain pointers into
// NetworkProbe. sampledAtMs is the local millis() value at the end of the
// connection attempt; zero means that target has not been sampled yet.
struct NetworkProbeTargetSnapshot {
  char name[kNetworkProbeNameSize] = {};
  char host[kNetworkProbeHostSize] = {};
  uint16_t port = 443;
  int32_t latencyMs = kNetworkProbeUnavailableMs;
  uint32_t sampledAtMs = 0;
  bool reachable = false;
};

struct NetworkProbeSnapshot {
  NetworkProbeTargetSnapshot targets[kNetworkProbeTargetCount];
  uint32_t updatedAtMs = 0;
  uint32_t completedRounds = 0;
};

// begin() and loop() must be called by the same (normally Arduino main-loop)
// task. snapshot() is protected by a critical section and may also be read by
// the HTTP task on the other ESP32 core.
//
// loop() performs at most one plain TCP connection attempt per call. The first
// round after Wi-Fi connects uses a short inter-target gap so startup can
// obtain all four results; later attempts are spaced 15 seconds apart, so each
// target is normally sampled once per minute. No TLS handshake or HTTP request
// is performed.
class NetworkProbe {
 public:
  void begin();
  void loop();

  NetworkProbeSnapshot snapshot() const;

 private:
  void markWifiDisconnected(uint32_t sampledAtMs);
  void probeTarget(size_t targetIndex);

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  NetworkProbeSnapshot snapshot_;
  bool begun_ = false;
  bool wifiWasConnected_ = false;
  bool offlinePublished_ = false;
  bool fastInitialRound_ = true;
  size_t nextTargetIndex_ = 0;
  uint32_t nextProbeAtMs_ = 0;
};

}  // namespace net
