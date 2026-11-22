#include "net/Connectivity.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include "board/BoardConfig.h"

namespace net {
namespace {

constexpr time_t kPlausibleEpoch = 1735689600;  // 2025-01-01T00:00:00Z
volatile bool gTimeSet = false;

void onTimeSync(struct timeval*) { gTimeSet = true; }

}  // namespace

bool clockPlausible() { return time(nullptr) > kPlausibleEpoch; }

bool Connectivity::hasStoredCredentials() {
  WiFi.mode(WIFI_STA);
  const bool saved = manager_.getWiFiIsSaved();
  return saved;
}

bool Connectivity::connect(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  // With no SSID argument the driver uses the credentials it stored in NVS.
  WiFi.begin();
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeoutMs) {
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool Connectivity::syncClock(uint32_t timeoutMs) {
  if (esp_sntp_enabled()) esp_sntp_stop();
  gTimeSet = false;
  // The notification fires when SNTP actually sets the clock; polling the
  // sync status proved unreliable with the Arduino configTzTime wrapper.
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(board::kTimeZone, "ntp.aliyun.com", "ntp.tencent.com",
               "pool.ntp.org");
  const uint32_t started = millis();
  while (!gTimeSet && millis() - started < timeoutMs) delay(50);
  const bool synced = gTimeSet;
  esp_sntp_stop();
  return synced && clockPlausible();
}

void Connectivity::disconnect() {
  if (portalStarted_) stopPortal();
  WiFi.disconnect(true, false);  // radio off, keep stored credentials
  WiFi.mode(WIFI_OFF);
}

void Connectivity::startPortal() {
  if (portalStarted_) return;
  manager_.setDebugOutput(false);
  manager_.setConfigPortalBlocking(false);
  manager_.setConfigPortalTimeout(board::kPortalTimeoutMs / 1000);
  WiFi.mode(WIFI_STA);
  manager_.startConfigPortal(board::kProvisioningApName);
  portalStarted_ = manager_.getConfigPortalActive();
}

void Connectivity::loopPortal() {
  if (!portalStarted_) return;
  manager_.process();
  if (!manager_.getConfigPortalActive()) portalStarted_ = false;
}

void Connectivity::stopPortal() {
  if (!portalStarted_) return;
  manager_.stopConfigPortal();
  portalStarted_ = false;
}

bool Connectivity::portalActive() { return portalStarted_; }

app::NetworkSnapshot Connectivity::snapshot() const {
  app::NetworkSnapshot snapshot;
  snapshot.connected = WiFi.status() == WL_CONNECTED;
  if (snapshot.connected) snapshot.rssi = static_cast<int16_t>(WiFi.RSSI());
  return snapshot;
}

}  // namespace net
