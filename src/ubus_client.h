#pragma once

#include <ArduinoJson.h>

#include "snapshot.h"

// JSON-RPC client for OpenWrt ubus on the ClusterBox BMC.
class UbusClient {
 public:
  bool begin();
  // Logs in, or refreshes when the session is close to the 300s timeout.
  bool ensureSession();
  // params is a JSON object, for example "{}" or "{\"path\":\"/etc/mixtile-release\"}".
  bool call(const char *object, const char *method, JsonDocument &out, const char *params = "{}");
  void dropSession();

 private:
  bool post(const char *body, JsonDocument &out);

  char session_[33] = {};
  uint32_t login_ms_ = 0;
};
