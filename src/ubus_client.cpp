#include "ubus_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"

static const char *kZeroSession = "00000000000000000000000000000000";

bool UbusClient::begin() {
  session_[0] = 0;
  login_ms_ = 0;
  return true;
}

void UbusClient::dropSession() {
  session_[0] = 0;
  login_ms_ = 0;
}

bool UbusClient::post(const char *body, JsonDocument &out) {
  if (WiFi.status() != WL_CONNECTED)
    return false;

  WiFiClient wifi;
  HTTPClient http;
  char url[64];
  snprintf(url, sizeof(url), "http://%s/ubus/", CLUSTER_HOST);

  if (!http.begin(wifi, url))
    return false;
  http.setTimeout(4000);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body)), strlen(body));
  if (code != 200) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  return deserializeJson(out, payload) == DeserializationError::Ok;
}

bool UbusClient::ensureSession() {
  if (session_[0] && (millis() - login_ms_) < kSessionRenewMs)
    return true;

  char body[256];
  snprintf(body, sizeof(body),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"call\",\"params\":[\"%s\","
           "\"session\",\"login\",{\"username\":\"%s\",\"password\":\"%s\"}]}",
           kZeroSession, CLUSTER_USER, CLUSTER_PASSWORD);

  JsonDocument doc;
  if (!post(body, doc))
    return false;
  if (doc["error"].is<JsonObject>())
    return false;
  if ((doc["result"][0] | -1) != 0)
    return false;

  const char *sid = doc["result"][1]["ubus_rpc_session"] | "";
  if (strlen(sid) < 32)
    return false;
  strncpy(session_, sid, sizeof(session_) - 1);
  session_[sizeof(session_) - 1] = 0;
  login_ms_ = millis();
  return true;
}

bool UbusClient::call(const char *object, const char *method, JsonDocument &out, const char *params) {
  if (!ensureSession())
    return false;
  if (!params || !params[0])
    params = "{}";

  char body[320];
  snprintf(body, sizeof(body),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"call\",\"params\":[\"%s\",\"%s\",\"%s\",%s]}",
           session_, object, method, params);

  if (!post(body, out))
    return false;
  if (out["error"].is<JsonObject>()) {
    dropSession();
    return false;
  }
  if ((out["result"][0] | -1) != 0) {
    dropSession();
    return false;
  }
  return true;
}
