#pragma once

// WIFI_SSID, WIFI_PASSWORD, CLUSTER_HOST, CLUSTER_USER and CLUSTER_PASSWORD
// come from .env through scripts/load_env.py. They are not stored in the tree.

// How often to pull nodectl status. LuCI polls the same call every 5s.
static constexpr uint32_t kPollMs = 5000;

// Hostname, firmware and interface addresses move slowly.
static constexpr uint32_t kIdentityMs = 30000;

// ubus sessions on this firmware last 300s.
static constexpr uint32_t kSessionRenewMs = 240000;

// Landscape 320x240. Use 3 if the picture is upside down.
static constexpr uint8_t kPanelRotation = 1;
