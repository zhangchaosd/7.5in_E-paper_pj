#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "SettingsStore.h"
#include "app/RuntimeState.h"

namespace net {

// WebServer is serviced exclusively by a task pinned to core 0. HTTP handlers
// only update state or settings; they never invoke the e-paper driver.
class DeviceServer {
 public:
  DeviceServer(app::RuntimeState& state, SettingsStore& settings,
               const char* firmwareVersion = "p1");
  ~DeviceServer();

  DeviceServer(const DeviceServer&) = delete;
  DeviceServer& operator=(const DeviceServer&) = delete;

  bool begin();
  void end();
  bool running() const;

 private:
  static void taskEntry(void* context);
  void run();
  void configureRoutes();

  void handleInfo();
  void handleHealth();
  void handleBridge();
  void handleDisplay();
  void handleRefresh();
  void handleNotFound();

  void sendJson(int statusCode, const String& body);
  void sendError(int statusCode, const char* message);

  app::RuntimeState& state_;
  SettingsStore& settings_;
  WebServer server_{80};
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool stopRequested_ = false;
  volatile bool running_ = false;
  bool routesConfigured_ = false;
  char firmwareVersion_[32] = {};
};

}  // namespace net
