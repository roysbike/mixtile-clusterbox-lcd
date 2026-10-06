#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cluster_poll.h"
#include "config.h"
#include "snapshot.h"
#include "status_view.h"
#include "touch_panel.h"
#include "ubus_client.h"
#include "wifi_config.h"

TFT_eSPI tft;
TouchPanel touch;
UbusClient ubus;

static ClusterSnapshot snap;
static LinkState bmcLink = LinkState::NeedWifi;
static Region pressed = Region::None;
static bool splash = true;
static bool wantPortal = false;
static uint32_t wifiQuietUntil = 0;

static void setLed(bool red, bool green, bool blue) {
  digitalWrite(LED_PIN_RED, red ? LOW : HIGH);
  digitalWrite(LED_PIN_GREEN, green ? LOW : HIGH);
  digitalWrite(LED_PIN_BLUE, blue ? LOW : HIGH);
}

static void ledForLink() {
  if (bmcLink == LinkState::Ok)
    setLed(false, true, false);
  else if (bmcLink == LinkState::PollFail)
    setLed(true, true, false);
  else if (bmcLink == LinkState::NeedWifi || bmcLink == LinkState::WifiDown)
    setLed(false, false, true);
  else
    setLed(true, false, false);
}

static void logSnap() {
  Serial.printf("bmc host=%s fw=%s wan=%s lan=%s miop=%s rail=%d fan=%d up=%d load=%s\n",
                snap.hostname, snap.firmware, snap.wan_ip, snap.lan_ip, snap.miop_ip,
                snap.ext_power, snap.fan_duty, snap.uptime_s, snap.loadavg);
  for (int i = 0; i < 4; i++) {
    const NodeView &n = snap.nodes[i];
    Serial.printf("  node%d name=%s power=%d pcie=%d link=%d ip=%s reach=%d busy=%s\n",
                  i + 1, n.name, n.power, n.pcie, n.pcie_link, n.ip[0] ? n.ip : "-",
                  n.reachable, n.busy);
  }
}

// Taps are reported only. Binding power/reset here would make a sloppy
// resistive press cut a blade.
static void onTap(Region region) {
  Serial.printf("tap region %d\n", static_cast<int>(region));
}

static void onHold(Region region) {
  Serial.printf("hold region %d\n", static_cast<int>(region));
}

static void serviceTouch() {
  static bool wasDown = false;
  static Region origin = Region::None;
  static int x0 = 0, y0 = 0;
  static uint32_t downMs = 0;
  static bool holdFired = false;
  static bool portalArmed = false;

  TouchSample sample = touch.read();
  if (sample.down && !wasDown) {
    origin = statusHitTest(sample.x, sample.y);
    x0 = sample.x;
    y0 = sample.y;
    downMs = millis();
    holdFired = false;
    pressed = origin;
  } else if (sample.down && wasDown) {
    if (abs(sample.x - x0) + abs(sample.y - y0) > 28)
      pressed = Region::None;
  } else if (!sample.down && wasDown) {
    if (!holdFired && pressed != Region::None)
      onTap(origin);
    pressed = Region::None;
  }

  if (sample.down && pressed != Region::None && !holdFired && millis() - downMs > 800) {
    holdFired = true;
    onHold(origin);
  }
  if (!sample.down) {
    portalArmed = false;
  } else if (!portalArmed && origin == Region::Bmc && pressed == Region::Bmc &&
             millis() - downMs > 3000) {
    portalArmed = true;
    holdFired = true;
    wantPortal = true;
  }
  wasDown = sample.down;
}

static void serviceBoot() {
  static uint32_t held = 0;
  if (digitalRead(BOOT_PIN) != LOW) {
    held = 0;
    return;
  }
  if (held == 0)
    held = millis();
  if (millis() - held < 2000)
    return;
  held = 0;
  while (digitalRead(BOOT_PIN) == LOW)
    delay(10);
  touch.calibrate(tft);
  splash = true;
  statusInvalidate();
}

static void serviceWifi() {
  static uint32_t lastTry = 0;
  if (!wifiHasCredentials()) {
    bmcLink = LinkState::NeedWifi;
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (bmcLink == LinkState::NeedWifi || bmcLink == LinkState::WifiDown)
      bmcLink = snap.status_ok ? LinkState::Ok : LinkState::PollFail;
    return;
  }
  bmcLink = LinkState::WifiDown;
  if ((int32_t)(millis() - wifiQuietUntil) < 0)
    return;
  if (lastTry != 0 && millis() - lastTry < 8000)
    return;
  WiFi.disconnect(false, false);
  WiFi.begin(wifiSsid(), wifiPass());
  lastTry = millis();
}

static void servicePoll() {
  static uint32_t lastStatus = 0;
  static uint32_t lastIdentity = 0;
  if (WiFi.status() != WL_CONNECTED)
    return;

  uint32_t now = millis();
  if (lastStatus == 0 || now - lastStatus >= kPollMs) {
    lastStatus = now;
    if (!ubus.ensureSession()) {
      bmcLink = LinkState::AuthFail;
    } else if (!pollStatus(ubus, snap)) {
      bmcLink = LinkState::PollFail;
    } else {
      bmcLink = LinkState::Ok;
      logSnap();
    }
  }
  if (bmcLink == LinkState::Ok && (lastIdentity == 0 || now - lastIdentity >= kIdentityMs)) {
    lastIdentity = now;
    pollIdentity(ubus, snap);
  }
}

static void paint() {
  static LinkState shown = LinkState::NeedWifi;
  if (!snap.status_ok) {
    if (!splash && bmcLink == shown)
      return;
    splash = false;
    shown = bmcLink;
    if (!wifiHasCredentials())
      statusMessage(tft, "WiFi setup", "ClusterBox-LCD");
    else if (bmcLink == LinkState::WifiDown || bmcLink == LinkState::NeedWifi)
      statusMessage(tft, "WiFi", wifiSsid());
    else if (bmcLink == LinkState::AuthFail)
      statusMessage(tft, "BMC login failed", CLUSTER_HOST);
    else
      statusMessage(tft, "Reading cluster", CLUSTER_HOST);
    return;
  }
  if (splash) {
    splash = false;
    statusInvalidate();
  }
  statusDraw(tft, snap, bmcLink, pressed);
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(LED_PIN_RED, OUTPUT);
  pinMode(LED_PIN_GREEN, OUTPUT);
  pinMode(LED_PIN_BLUE, OUTPUT);
  setLed(false, false, true);

  memset(&snap, 0, sizeof(snap));
  snap.fan_duty = -1;
  snap.temp_mc = -1;
  snap.ext_power = -1;

  tft.init();
  tft.setRotation(kPanelRotation);
  tft.fillScreen(TFT_BLACK);

  touch.begin();
  if (!touch.calibrated())
    touch.calibrate(tft);

  wifiLoad();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("clusterbox-lcd");
  ubus.begin();
  splash = true;
}

void loop() {
  static uint32_t offlineSince = 0;

  serviceBoot();
  serviceTouch();

  if (WiFi.status() == WL_CONNECTED)
    offlineSince = 0;
  else if (wifiHasCredentials() && offlineSince == 0)
    offlineSince = millis();

  bool openPortal = wantPortal || !wifiHasCredentials() ||
                    (offlineSince && millis() - offlineSince > 25000);
  if (openPortal) {
    bool allowCancel = wifiHasCredentials();
    wantPortal = false;
    bool joined = wifiPortal(tft, allowCancel);
    wifiQuietUntil = millis() + 8000;
    offlineSince = joined ? 0 : millis();
    splash = true;
    statusInvalidate();
  }

  serviceWifi();
  servicePoll();
  ledForLink();
  paint();
  delay(20);
}
