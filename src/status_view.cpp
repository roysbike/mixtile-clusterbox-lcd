#include "status_view.h"

#include <stdio.h>
#include <string.h>

#include "board.h"

static uint16_t cBg, cCard, cText, cDim, cOk, cWarn, cMute, cLine, cKube, cKubeDim, cBad;
static bool colorsReady = false;
static bool force = true;
static NodeView drawnNodes[4];
static char drawnBmc[192];
static LinkState drawnLink = LinkState::NeedWifi;
static Region drawnPress = Region::None;

static void ensureColors(TFT_eSPI &tft) {
  if (colorsReady)
    return;
  cBg = tft.color565(11, 16, 28);
  cCard = tft.color565(22, 30, 46);
  cText = tft.color565(236, 240, 244);
  cDim = tft.color565(148, 160, 178);
  cOk = tft.color565(61, 204, 138);
  cWarn = tft.color565(224, 164, 48);
  cMute = tft.color565(96, 108, 128);
  cLine = tft.color565(36, 48, 68);
  cKube = tft.color565(50, 108, 229);
  cKubeDim = tft.color565(28, 58, 120);
  cBad = tft.color565(220, 80, 70);
  colorsReady = true;
}

struct Rect {
  int x, y, w, h;
};

static Rect cellRect(int index) {
  int col = index % 2;
  int row = index / 2;
  return {col * (kCellW + kGap), kTopH + row * (kCellH + kGap), kCellW, kCellH};
}

static Rect regionRect(Region region) {
  if (region == Region::Bmc)
    return {0, 0, PANEL_W, kTopH};
  return cellRect(static_cast<int>(region) - static_cast<int>(Region::Node1));
}

void statusInvalidate() {
  force = true;
  drawnBmc[0] = 0;
  memset(drawnNodes, 0, sizeof(drawnNodes));
}

void statusMessage(TFT_eSPI &tft, const char *title, const char *detail) {
  ensureColors(tft);
  tft.fillScreen(cBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(cText, cBg);
  tft.drawString(title, PANEL_W / 2, PANEL_H / 2 - 14);
  tft.setTextFont(1);
  tft.setTextColor(cDim, cBg);
  tft.drawString(detail ? detail : "", PANEL_W / 2, PANEL_H / 2 + 12);
  statusInvalidate();
}

Region statusHitTest(int x, int y) {
  if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H)
    return Region::None;
  if (y < kTopH)
    return Region::Bmc;
  int col = x * 2 / PANEL_W;
  int row = (y - kTopH) * 2 / (PANEL_H - kTopH);
  if (col < 0 || col > 1 || row < 0 || row > 1)
    return Region::None;
  return static_cast<Region>(static_cast<int>(Region::Node1) + row * 2 + col);
}

static void fmtUptime(int sec, char *out, size_t n) {
  if (sec < 0) {
    snprintf(out, n, "--");
    return;
  }
  int d = sec / 86400;
  int h = (sec % 86400) / 3600;
  int m = (sec % 3600) / 60;
  if (d)
    snprintf(out, n, "%dd%dh", d, h);
  else if (h)
    snprintf(out, n, "%dh%dm", h, m);
  else
    snprintf(out, n, "%dm", m);
}

static void firstToken(const char *src, char *out, size_t n) {
  size_t i = 0;
  while (src[i] && src[i] != ' ' && i + 1 < n) {
    out[i] = src[i];
    i++;
  }
  out[i] = 0;
  if (!out[0])
    snprintf(out, n, "--");
}

static uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t >> 8);
  int g = ag + ((bg - ag) * t >> 8);
  int bl = ab + ((bb - ab) * t >> 8);
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

static uint8_t pulse(uint32_t periodMs) {
  uint32_t half = periodMs / 2;
  uint32_t p = millis() % periodMs;
  if (p > half)
    p = periodMs - p;
  return static_cast<uint8_t>(p * 255 / half);
}

static void nodeState(const NodeView &node, const char *&label, uint16_t &color, bool &alive) {
  alive = false;
  if (node.id == 0) {
    label = "Unknown";
    color = cMute;
  } else if (node.busy[0]) {
    label = "Pending";
    color = cWarn;
    alive = true;
  } else if (node.power == 0) {
    label = "Off";
    color = cMute;
  } else if (node.reachable == 1) {
    label = "Ready";
    color = cOk;
    alive = true;
  } else if (node.reachable == 0) {
    label = "NotReady";
    color = cWarn;
    alive = true;
  } else {
    label = "Pending";
    color = cWarn;
    alive = true;
  }
}

static const char *pcieLabel(const NodeView &node) {
  if (node.id == 0)
    return "PCIe Unknown";
  if (node.pcie == 1)
    return "PCIe Ready";
  if (node.pcie_link == 1)
    return "PCIe Link";
  return "PCIe NotReady";
}

static uint16_t linkColor(LinkState link) {
  if (link == LinkState::Ok)
    return cOk;
  if (link == LinkState::PollFail)
    return cWarn;
  return cBad;
}

static void drawIp(TFT_eSPI &tft, int x, int y, int maxW, const char *label, const char *ip) {
  const char *value = (ip && ip[0]) ? ip : "--";
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(cKube, cCard);
  tft.drawString(label, x, y);
  tft.setTextFont(2);
  tft.setTextColor(cText, cCard);
  if (tft.textWidth(value) > maxW) {
    tft.setTextFont(1);
    tft.drawString(value, x, y + 12);
  } else {
    tft.drawString(value, x, y + 10);
  }
}

static void bmcSignature(const ClusterSnapshot &snap, LinkState link, char *out, size_t n) {
  snprintf(out, n, "%s|%s|%s|%s|%s|%d|%d|%d|%d|%s|%u",
           snap.hostname, snap.firmware, snap.wan_ip, snap.lan_ip, snap.miop_ip,
           snap.ext_power, snap.fan_duty, snap.temp_mc, snap.uptime_s, snap.loadavg,
           static_cast<unsigned>(link));
}

static void drawBmc(TFT_eSPI &tft, const ClusterSnapshot &snap, LinkState link) {
  tft.fillRect(0, 0, PANEL_W, kTopH, cCard);
  tft.fillRect(0, 0, PANEL_W, 3, linkColor(link));

  tft.fillRoundRect(8, 8, 28, 14, 3, cKube);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(cText, cKube);
  tft.drawString("k8s", 22, 15);

  const char *host = snap.hostname[0] ? snap.hostname : "ClusterBox";
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(cText, cCard);
  tft.drawString(host, 42, 6);

  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(cDim, cCard);
  if (snap.firmware[0])
    tft.drawString(snap.firmware, PANEL_W - 8, 8);

  char up[12], load[8], meta[48];
  fmtUptime(snap.uptime_s, up, sizeof(up));
  firstToken(snap.loadavg, load, sizeof(load));
  if (snap.fan_duty < 0)
    snprintf(meta, sizeof(meta), "%s  %s", up, load);
  else
    snprintf(meta, sizeof(meta), "fan %d%%  %s  %s", snap.fan_duty, up, load);
  tft.drawString(meta, PANEL_W - 8, 18);

  int half = PANEL_W / 2;
  const char *lan = snap.miop_ip[0] ? snap.miop_ip : snap.lan_ip;
  drawIp(tft, 8, 32, half - 20, "WAN", snap.wan_ip);
  drawIp(tft, half + 8, 32, half - 20, "LAN", lan);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(cDim, cCard);
  tft.drawString("miop", half + 32, 32);
  tft.drawFastHLine(0, kTopH - 1, PANEL_W, cLine);
}

static void drawNode(TFT_eSPI &tft, int index, const NodeView &node) {
  Rect r = cellRect(index);
  tft.fillRect(r.x, r.y, r.w, r.h, cCard);

  const char *state = "Unknown";
  uint16_t col = cMute;
  bool alive = false;
  nodeState(node, state, col, alive);
  tft.fillRect(r.x, r.y, 4, r.h, col);

  char title[24];
  snprintf(title, sizeof(title), "%s", node.name[0] ? node.name : "node");
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(cText, cCard);
  tft.drawString(title, r.x + 12, r.y + 6);

  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(cDim, cCard);
  tft.drawString("OS: talos", r.x + r.w - 8, r.y + 10);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(col, cCard);
  tft.drawString(state, r.x + 20, r.y + 28);

  tft.setTextColor(node.ip[0] ? cText : cMute, cCard);
  tft.drawString(node.ip[0] ? node.ip : "no IP", r.x + 12, r.y + 48);

  tft.setTextFont(1);
  tft.setTextColor(node.pcie == 1 ? cOk : cDim, cCard);
  tft.drawString(pcieLabel(node), r.x + 12, r.y + 66);
}

static void drawDot(TFT_eSPI &tft, int index, const NodeView &node) {
  const char *state = "Unknown";
  uint16_t col = cMute;
  bool alive = false;
  nodeState(node, state, col, alive);
  Rect r = cellRect(index);
  uint8_t level = alive ? pulse(node.reachable == 1 ? 1400 : 700) : 255;
  uint16_t c = alive ? mix(cCard, col, 70 + (level >> 2)) : col;
  tft.fillCircle(r.x + 12, r.y + 36, 3, c);
}

static void animate(TFT_eSPI &tft, const ClusterSnapshot &snap, LinkState link) {
  uint16_t base = linkColor(link);
  tft.fillRect(0, 0, PANEL_W, 3, base);
  if (link == LinkState::Ok) {
    int travel = PANEL_W + 40;
    int x = static_cast<int>((millis() / 12) % travel) - 40;
    int x0 = x < 0 ? 0 : x;
    int x1 = x + 40 > PANEL_W ? PANEL_W : x + 40;
    if (x1 > x0)
      tft.fillRect(x0, 0, x1 - x0, 3, mix(base, cText, 200));
  }
  for (int i = 0; i < 4; i++)
    drawDot(tft, i, snap.nodes[i]);
}

static void redrawRegion(TFT_eSPI &tft, const ClusterSnapshot &snap, LinkState link, Region region) {
  if (region == Region::Bmc) {
    drawBmc(tft, snap, link);
    return;
  }
  int index = static_cast<int>(region) - static_cast<int>(Region::Node1);
  if (index >= 0 && index < 4)
    drawNode(tft, index, snap.nodes[index]);
}

static void drawPressed(TFT_eSPI &tft, Region pressed) {
  if (pressed == Region::None)
    return;
  Rect r = regionRect(pressed);
  tft.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, cKube);
}

void statusDraw(TFT_eSPI &tft, const ClusterSnapshot &snap, LinkState link, Region pressed) {
  ensureColors(tft);
  char sig[192];
  bmcSignature(snap, link, sig, sizeof(sig));

  if (force) {
    tft.fillScreen(cBg);
    drawBmc(tft, snap, link);
    for (int i = 0; i < 4; i++)
      drawNode(tft, i, snap.nodes[i]);
    drawnPress = Region::None;
  } else {
    if (strcmp(sig, drawnBmc) != 0 || link != drawnLink)
      drawBmc(tft, snap, link);
    for (int i = 0; i < 4; i++) {
      if (memcmp(&drawnNodes[i], &snap.nodes[i], sizeof(NodeView)) != 0)
        drawNode(tft, i, snap.nodes[i]);
    }
    if (pressed != drawnPress && drawnPress != Region::None)
      redrawRegion(tft, snap, link, drawnPress);
  }

  drawPressed(tft, pressed);
  animate(tft, snap, link);
  drawnPress = pressed;
  memcpy(drawnNodes, snap.nodes, sizeof(drawnNodes));
  strncpy(drawnBmc, sig, sizeof(drawnBmc) - 1);
  drawnBmc[sizeof(drawnBmc) - 1] = 0;
  drawnLink = link;
  force = false;
}
