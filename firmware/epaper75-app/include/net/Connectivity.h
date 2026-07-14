#pragma once

#include <Arduino.h>
#include <WiFiManager.h>

#include "app/RuntimeState.h"

namespace net {

class Connectivity {
 public:
  explicit Connectivity(app::RuntimeState& state);

  // Non-blocking. Starts an ESP-IDF connection using credentials already in
  // NVS. If that has not connected after a grace period, loop() starts the
  // non-blocking WiFiManager portal named Epaper75-Setup.
  bool begin();
  void loop();

  app::NetworkSnapshot snapshot() const;
  bool portalActive() const;

 private:
  void publishSnapshot(bool force);
  void setPortalActive(bool value);

  app::RuntimeState& state_;
  WiFiManager manager_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  app::NetworkSnapshot last_;
  bool begun_ = false;
  bool portalActive_ = false;
  bool everConnected_ = false;
  uint32_t connectionStartedAtMs_ = 0;
  uint32_t lastSnapshotAtMs_ = 0;
};

}  // namespace net
