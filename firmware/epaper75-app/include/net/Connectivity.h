#pragma once

#include <Arduino.h>
#include <WiFiManager.h>

#include "app/Models.h"

namespace net {

// Wi-Fi is switched on only for the duration of a fetch. Station credentials
// are the ones the ESP32 Wi-Fi driver already keeps in its own NVS namespace
// (written by the WiFiManager portal); this class never erases them.
class Connectivity {
 public:
  bool hasStoredCredentials();

  // Blocking station connect with stored credentials.
  bool connect(uint32_t timeoutMs);
  // Fresh SNTP synchronisation (UTC+8 zone). Returns true once completed.
  bool syncClock(uint32_t timeoutMs);
  // Radio fully off to save power.
  void disconnect();

  // Non-blocking provisioning portal "Epaper75-Setup" (interactive mode).
  void startPortal();
  void loopPortal();
  void stopPortal();
  bool portalActive();

  app::NetworkSnapshot snapshot() const;

 private:
  WiFiManager manager_;
  bool portalStarted_ = false;
};

// True when the system clock holds a plausible synchronized time.
bool clockPlausible();

}  // namespace net
