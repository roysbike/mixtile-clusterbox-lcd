#pragma once

#include <stdint.h>

class TFT_eSPI;

// Saved network, or the optional defaults from include/secrets.h.
bool wifiLoad();
bool wifiHasCredentials();
const char *wifiSsid();
const char *wifiPass();

// Access point ClusterBox-LCD, page http://192.168.4.1
// Returns true when the panel joined a network.
// allowCancel: BOOT leaves the previous network in place.
bool wifiPortal(TFT_eSPI &tft, bool allowCancel);
