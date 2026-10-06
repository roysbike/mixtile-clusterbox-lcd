#pragma once

#include <stdint.h>

// Mirrors `nodectl status --json` plus a few BMC identity fields.
// Tri-state ints are -1 when the BMC sent JSON null.

struct NodeView {
  int id;
  char name[16];
  int8_t power;
  int8_t pcie;
  int8_t pcie_link;
  int prz;
  char ip[16];
  char check[16];
  int8_t reachable;
  char busy[24];
  int8_t tty_present;
  int8_t console_server;
  int8_t autostart;
};

struct ClusterSnapshot {
  NodeView nodes[4];

  int8_t ext_power;
  int fan_duty;
  int temp_mc;

  int uptime_s;
  char loadavg[24];
  int mem_total_kb;
  int mem_avail_kb;

  char hostname[32];
  char firmware[32];
  char wan_ip[16];
  char lan_ip[16];
  char miop_ip[16];

  uint32_t fetched_ms;
  bool status_ok;
};

enum class Region : int8_t {
  None = -1,
  Bmc = 0,
  Node1 = 1,
  Node2 = 2,
  Node3 = 3,
  Node4 = 4,
};

enum class LinkState : uint8_t {
  NeedWifi,
  WifiDown,
  AuthFail,
  PollFail,
  Ok,
};
