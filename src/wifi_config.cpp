#include "wifi_config.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#include "board.h"
#include "config.h"

static const char *kApSsid = "ClusterBox-LCD";
static const IPAddress kApIp(192, 168, 4, 1);

static char ssid_[33];
static char pass_[65];

static void copyClip(char *dst, size_t n, const char *src) {
  if (!src) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

bool wifiLoad() {
  memset(ssid_, 0, sizeof(ssid_));
  memset(pass_, 0, sizeof(pass_));

  Preferences prefs;
  if (prefs.begin("wifi", true)) {
    prefs.getString("ssid", ssid_, sizeof(ssid_));
    prefs.getString("pass", pass_, sizeof(pass_));
    prefs.end();
  }
  if (ssid_[0])
    return true;
  if (strcmp(WIFI_SSID, "changeme") != 0 && WIFI_SSID[0]) {
    copyClip(ssid_, sizeof(ssid_), WIFI_SSID);
    copyClip(pass_, sizeof(pass_), WIFI_PASSWORD);
    return true;
  }
  return false;
}

bool wifiHasCredentials() {
  return ssid_[0] != 0;
}

const char *wifiSsid() {
  return ssid_;
}

const char *wifiPass() {
  return pass_;
}

static bool wifiSave(const char *ssid, const char *pass) {
  Preferences prefs;
  if (!prefs.begin("wifi", false))
    return false;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass ? pass : "");
  prefs.end();
  copyClip(ssid_, sizeof(ssid_), ssid);
  copyClip(pass_, sizeof(pass_), pass ? pass : "");
  return true;
}

static String htmlEscape(const char *text) {
  String out;
  for (const char *p = text; *p; p++) {
    if (*p == '&')
      out += F("&amp;");
    else if (*p == '<')
      out += F("&lt;");
    else if (*p == '"')
      out += F("&quot;");
    else
      out += *p;
  }
  return out;
}

static void drawPortal(TFT_eSPI &tft, const char *title, const char *detail, bool allowCancel) {
  uint16_t bg = tft.color565(16, 18, 22);
  uint16_t fg = tft.color565(232, 236, 240);
  uint16_t dim = tft.color565(154, 163, 175);
  uint16_t accent = tft.color565(96, 196, 232);
  tft.fillScreen(bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(fg, bg);
  tft.drawString(title, PANEL_W / 2, 78);
  tft.setTextColor(accent, bg);
  tft.drawString(kApSsid, PANEL_W / 2, 118);
  tft.drawString("192.168.4.1", PANEL_W / 2, 150);
  tft.setTextFont(1);
  tft.setTextColor(dim, bg);
  tft.drawString(detail, PANEL_W / 2, 190);
  if (allowCancel)
    tft.drawString("BOOT cancels", PANEL_W / 2, 214);
}

static String portalPage(bool failed, char nets[][33], int netCount) {
  String page;
  page.reserve(2048);
  page += F("<!doctype html><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>ClusterBox</title><style>"
            "body{font:18px sans-serif;background:#12161a;color:#e8ecf0;margin:0;padding:20px}"
            "h1{font-size:22px;margin:0 0 8px}"
            "p{color:#9aa3af;margin:0 0 12px}"
            ".err{color:#e07070}"
            "label{display:block;margin:14px 0 6px}"
            "input,select,button{font:18px sans-serif;width:100%;box-sizing:border-box;padding:12px}"
            "button{background:#2f9e6b;color:#fff;border:0;margin-top:18px}"
            "</style><h1>Сеть панели</h1>"
            "<p>Только 2.4 ГГц. Пароль сохранится в памяти экрана.</p>");
  if (failed)
    page += F("<p class=err>Не подключилось. Проверьте имя и пароль.</p>");
  page += F("<form method=POST action=/save><label>Сеть</label><select name=ssid>");
  if (netCount <= 0)
    page += F("<option value=\"\">сети не найдены</option>");
  for (int i = 0; i < netCount; i++) {
    String name = htmlEscape(nets[i]);
    page += F("<option value=\"");
    page += name;
    page += F("\">");
    page += name;
    page += F("</option>");
  }
  page += F("</select><label>Или имя вручную</label>"
            "<input name=custom maxlength=32 autocapitalize=none autocorrect=off>"
            "<label>Пароль</label>"
            "<input name=pass type=password maxlength=64>"
            "<button type=submit>Сохранить</button></form>");
  return page;
}

bool wifiPortal(TFT_eSPI &tft, bool allowCancel) {
  char nets[12][33];
  int netCount = -1;
  uint32_t scannedAt = 0;
  bool done = false;
  bool failed = false;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(kApSsid);
  digitalWrite(LED_PIN_RED, HIGH);
  digitalWrite(LED_PIN_GREEN, HIGH);
  digitalWrite(LED_PIN_BLUE, LOW);
  drawPortal(tft, "WiFi setup", "open in a browser", allowCancel);

  DNSServer dns;
  dns.start(53, "*", kApIp);
  WebServer server(80);

  auto refreshScan = [&]() {
    if (netCount >= 0 && millis() - scannedAt < 15000)
      return;
    int found = WiFi.scanNetworks();
    netCount = 0;
    for (int i = 0; i < found && netCount < 12; i++) {
      String name = WiFi.SSID(i);
      if (!name.length() || name.length() > 32)
        continue;
      bool dup = false;
      for (int j = 0; j < netCount; j++) {
        if (name == nets[j])
          dup = true;
      }
      if (dup)
        continue;
      copyClip(nets[netCount], sizeof(nets[netCount]), name.c_str());
      netCount++;
    }
    WiFi.scanDelete();
    scannedAt = millis();
  };

  auto sendPage = [&]() {
    refreshScan();
    server.send(200, "text/html", portalPage(failed, nets, netCount < 0 ? 0 : netCount));
  };

  server.on("/", HTTP_GET, sendPage);
  server.on("/save", HTTP_POST, [&]() {
    String custom = server.arg("custom");
    custom.trim();
    String chosen = custom.length() ? custom : server.arg("ssid");
    chosen.trim();
    String pw = server.arg("pass");
    if (!chosen.length() || chosen.length() > 32 || pw.length() > 64) {
      failed = true;
      sendPage();
      return;
    }

    drawPortal(tft, "Connecting", chosen.c_str(), false);
    WiFi.begin(chosen.c_str(), pw.c_str());
    uint32_t started = millis();
    while (millis() - started < 15000) {
      if (WiFi.status() == WL_CONNECTED)
        break;
      delay(80);
    }

    if (WiFi.status() != WL_CONNECTED) {
      failed = true;
      WiFi.disconnect(false, false);
      drawPortal(tft, "WiFi setup", "open in a browser", allowCancel);
      sendPage();
      return;
    }

    wifiSave(chosen.c_str(), pw.c_str());
    done = true;
    drawPortal(tft, "Connected", chosen.c_str(), false);
    server.send(200, "text/html",
                "<!doctype html><meta charset=utf-8><h1>Подключено</h1>"
                "<p>Страницу можно закрыть. Панель переходит к ClusterBox.</p>");
  });

  auto bounce = [&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  };
  server.on("/generate_204", bounce);
  server.on("/gen_204", bounce);
  server.on("/hotspot-detect.html", bounce);
  server.on("/canonical.html", bounce);
  server.on("/connecttest.txt", bounce);
  server.on("/ncsi.txt", bounce);
  server.on("/fwlink", bounce);
  server.onNotFound(bounce);
  server.begin();

  while (!done) {
    dns.processNextRequest();
    server.handleClient();
    if (allowCancel && digitalRead(BOOT_PIN) == LOW) {
      delay(40);
      if (digitalRead(BOOT_PIN) == LOW) {
        while (digitalRead(BOOT_PIN) == LOW)
          delay(10);
        break;
      }
    }
    delay(10);
  }

  delay(done ? 400 : 0);
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  if (ssid_[0])
    WiFi.begin(ssid_, pass_);
  return done;
}
