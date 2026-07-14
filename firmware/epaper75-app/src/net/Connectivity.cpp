#include "net/Connectivity.h"

#include <WiFi.h>
#include <stdlib.h>
#include <string.h>

namespace net {
namespace {

constexpr char kPortalName[] = "Epaper75-Setup";
constexpr uint32_t kPortalGraceMs = 12000;
constexpr uint32_t kSnapshotIntervalMs = 1000;
constexpr int kRssiPublishDeltaDb = 5;

}  // namespace

Connectivity::Connectivity(app::RuntimeState& state) : state_(state) {}

bool Connectivity::begin() {
  if (begun_) return true;

  manager_.setDebugOutput(false);
  manager_.setConfigPortalBlocking(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  // With no SSID argument the ESP32 starts the connection stored by the Wi-Fi
  // driver in NVS. This is asynchronous and does not rewrite or erase it.
  WiFi.begin();
  connectionStartedAtMs_ = millis();
  begun_ = true;
  publishSnapshot(true);
  return true;
}

void Connectivity::loop() {
  if (!begun_) return;

  bool portal = manager_.getConfigPortalActive();
  if (portal) {
    manager_.process();
    portal = manager_.getConfigPortalActive();
  }

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && portal) {
    everConnected_ = true;
    manager_.stopConfigPortal();
    portal = false;
  } else if (connected) {
    everConnected_ = true;
  } else if (!everConnected_ && !portal &&
             static_cast<uint32_t>(millis() - connectionStartedAtMs_) >=
                 kPortalGraceMs) {
    manager_.setConfigPortalBlocking(false);
    manager_.startConfigPortal(kPortalName);
    portal = manager_.getConfigPortalActive();
    if (!portal) connectionStartedAtMs_ = millis();
  }

  setPortalActive(portal);
  publishSnapshot(false);
}

void Connectivity::publishSnapshot(bool force) {
  const uint32_t now = millis();
  if (!force &&
      static_cast<uint32_t>(now - lastSnapshotAtMs_) < kSnapshotIntervalMs) {
    return;
  }
  lastSnapshotAtMs_ = now;

  app::NetworkSnapshot next;
  next.connected = WiFi.status() == WL_CONNECTED;
  if (next.connected) {
    app::copyText(next.ssid, WiFi.SSID().c_str());
    app::copyText(next.ip, WiFi.localIP().toString().c_str());
    next.rssi = static_cast<int16_t>(WiFi.RSSI());
  }

  portENTER_CRITICAL(&mux_);
  if (next.connected && last_.connected &&
      strcmp(next.ssid, last_.ssid) == 0 && strcmp(next.ip, last_.ip) == 0 &&
      abs(static_cast<int>(next.rssi) - static_cast<int>(last_.rssi)) <
          kRssiPublishDeltaDb) {
    next.rssi = last_.rssi;
  }
  last_ = next;
  portEXIT_CRITICAL(&mux_);

  state_.updateNetwork(next);
}

void Connectivity::setPortalActive(bool value) {
  portENTER_CRITICAL(&mux_);
  portalActive_ = value;
  portEXIT_CRITICAL(&mux_);
}

app::NetworkSnapshot Connectivity::snapshot() const {
  portENTER_CRITICAL(&mux_);
  const app::NetworkSnapshot copy = last_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

bool Connectivity::portalActive() const {
  portENTER_CRITICAL(&mux_);
  const bool active = portalActive_;
  portEXIT_CRITICAL(&mux_);
  return active;
}

}  // namespace net
