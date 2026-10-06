#pragma once

#include <TFT_eSPI.h>

#include "board.h"
#include "snapshot.h"

// Landscape 320x240. Top band is the BMC.
// WAN is the uplink. LAN on this box is the MIOP fabric (node network).
// Below it, four worker tiles in a 2x2 grid. Each tile is the touch target.

static constexpr int kTopH = 78;
static constexpr int kGap = 4;
static constexpr int kCellW = (PANEL_W - kGap) / 2;
static constexpr int kCellH = (PANEL_H - kTopH - kGap) / 2;

void statusInvalidate();
void statusMessage(TFT_eSPI &tft, const char *title, const char *detail);
void statusDraw(TFT_eSPI &tft, const ClusterSnapshot &snap, LinkState link, Region pressed);
Region statusHitTest(int x, int y);
