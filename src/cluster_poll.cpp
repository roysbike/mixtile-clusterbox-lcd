#include "cluster_poll.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

static void copyStr(char *dst, size_t n, const char *src) {
  if (!src) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

static int8_t tri(JsonVariant v) {
  if (v.isNull())
    return -1;
  return v.as<bool>() ? 1 : 0;
}

static void parseRelease(const char *text, char *fw, size_t n) {
  const char *key = "FIRMWARE_VERSION=\"";
  const char *p = strstr(text, key);
  if (!p)
    return;
  p += strlen(key);
  const char *end = strchr(p, '"');
  if (!end)
    return;
  size_t len = static_cast<size_t>(end - p);
  if (len >= n)
    len = n - 1;
  memcpy(fw, p, len);
  fw[len] = 0;
}

bool pollStatus(UbusClient &ubus, ClusterSnapshot &snap) {
  JsonDocument doc;
  if (!ubus.call("nodectl", "status", doc))
    return false;

  JsonObject payload = doc["result"][1];
  JsonArray nodes = payload["nodes"].as<JsonArray>();
  if (nodes.isNull())
    return false;

  memset(snap.nodes, 0, sizeof(snap.nodes));
  for (JsonObject node : nodes) {
    int id = node["id"] | 0;
    if (id < 1 || id > 4)
      continue;
    NodeView &n = snap.nodes[id - 1];
    n.id = id;
    copyStr(n.name, sizeof(n.name), node["name"] | "");
    n.power = tri(node["power"]);
    n.pcie = tri(node["pcie"]);
    n.pcie_link = tri(node["pcie_link"]);
    n.prz = node["prz"] | 0;
    if (node["ipaddr"].isNull())
      n.ip[0] = 0;
    else
      copyStr(n.ip, sizeof(n.ip), node["ipaddr"] | "");
    if (node["check"].isNull())
      n.check[0] = 0;
    else
      copyStr(n.check, sizeof(n.check), node["check"] | "");
    n.reachable = tri(node["reachable"]);
    if (node["busy"].isNull())
      n.busy[0] = 0;
    else
      copyStr(n.busy, sizeof(n.busy), node["busy"] | "");
    n.tty_present = tri(node["tty_present"]);
    n.console_server = tri(node["console_server"]);
    n.autostart = tri(node["autostart"]);
  }

  JsonObject chassis = payload["chassis"];
  snap.ext_power = tri(chassis["ext_power"]);
  snap.fan_duty = chassis["fan_duty"].isNull() ? -1 : chassis["fan_duty"].as<int>();
  snap.temp_mc = chassis["temp_mc"].isNull() ? -1 : chassis["temp_mc"].as<int>();

  JsonObject host = payload["host"];
  snap.uptime_s = host["uptime"] | 0;
  copyStr(snap.loadavg, sizeof(snap.loadavg), host["loadavg"] | "");
  snap.mem_total_kb = host["mem_total_kb"] | 0;
  snap.mem_avail_kb = host["mem_avail_kb"] | 0;
  snap.fetched_ms = millis();
  snap.status_ok = true;
  return true;
}

static void setIp(char *dst, size_t n, const char *ip) {
  if (ip && ip[0])
    copyStr(dst, n, ip);
}

static void takeIpv4(JsonObject iface, char *dst, size_t n) {
  JsonArray addrs = iface["ipv4-address"].as<JsonArray>();
  if (addrs.isNull() || addrs.size() == 0)
    return;
  setIp(dst, n, addrs[0]["address"] | "");
}

bool pollIdentity(UbusClient &ubus, ClusterSnapshot &snap) {
  bool any = false;

  {
    JsonDocument doc;
    if (ubus.call("system", "board", doc)) {
      copyStr(snap.hostname, sizeof(snap.hostname), doc["result"][1]["hostname"] | "ClusterBox");
      any = true;
    }
  }
  {
    JsonDocument doc;
    if (ubus.call("file", "read", doc, "{\"path\":\"/etc/mixtile-release\"}")) {
      const char *text = doc["result"][1]["data"] | "";
      parseRelease(text, snap.firmware, sizeof(snap.firmware));
      any = true;
    }
  }
  {
    JsonDocument doc;
    if (ubus.call("network.interface", "dump", doc)) {
      for (JsonObject iface : doc["result"][1]["interface"].as<JsonArray>()) {
        const char *name = iface["interface"] | "";
        if (strcmp(name, "wan") == 0)
          takeIpv4(iface, snap.wan_ip, sizeof(snap.wan_ip));
        else if (strcmp(name, "lan") == 0)
          takeIpv4(iface, snap.lan_ip, sizeof(snap.lan_ip));
      }
      any = true;
    }
  }
  {
    JsonDocument doc;
    if (ubus.call("luci-rpc", "getNetworkDevices", doc)) {
      JsonObject devs = doc["result"][1];
      setIp(snap.miop_ip, sizeof(snap.miop_ip), devs["omi0"]["ipaddrs"][0]["address"] | "");
      setIp(snap.lan_ip, sizeof(snap.lan_ip), devs["br-lan"]["ipaddrs"][0]["address"] | "");
      setIp(snap.wan_ip, sizeof(snap.wan_ip), devs["eth0.2"]["ipaddrs"][0]["address"] | "");
      any = true;
    }
  }
  return any;
}
