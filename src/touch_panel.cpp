#include "touch_panel.h"

#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "board.h"
#include "config.h"

static constexpr uint32_t kCalMagic = 0x52323854;
static constexpr uint8_t kCmdX = 0xD0;
static constexpr uint8_t kCmdY = 0x90;
static constexpr uint8_t kCmdZ1 = 0xB1;
static constexpr int kZMin = 250;

static SPIClass touchSpi(VSPI);

struct StoredCal {
  uint32_t magic;
  uint8_t rotation;
  uint8_t swap;
  int16_t x_left;
  int16_t x_right;
  int16_t y_top;
  int16_t y_bottom;
};

void TouchPanel::begin() {
  touchSpi.begin(TP_PIN_SCK, TP_PIN_MISO, TP_PIN_MOSI, TP_PIN_CS);
  pinMode(TP_PIN_CS, OUTPUT);
  digitalWrite(TP_PIN_CS, HIGH);
  pinMode(TP_PIN_IRQ, INPUT);
  valid_ = load();
}

uint16_t TouchPanel::readCmd(uint8_t cmd) {
  touchSpi.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));
  digitalWrite(TP_PIN_CS, LOW);
  touchSpi.transfer(cmd);
  uint16_t hi = touchSpi.transfer(0);
  uint16_t lo = touchSpi.transfer(0);
  digitalWrite(TP_PIN_CS, HIGH);
  touchSpi.endTransaction();
  return ((hi << 8) | lo) >> 3 & 0x0FFF;
}

bool TouchPanel::sample(int &x, int &y, int &z) {
  if (digitalRead(TP_PIN_IRQ) != LOW)
    return false;
  z = readCmd(kCmdZ1);
  x = readCmd(kCmdX);
  y = readCmd(kCmdY);
  readCmd(0x80);
  return z >= kZMin;
}

bool TouchPanel::load() {
  Preferences prefs;
  if (!prefs.begin("touch", true))
    return false;
  StoredCal cal = {};
  size_t n = prefs.getBytes("cal", &cal, sizeof(cal));
  prefs.end();
  if (n != sizeof(cal) || cal.magic != kCalMagic || cal.rotation != kPanelRotation)
    return false;
  if (abs(cal.x_right - cal.x_left) < 400 || abs(cal.y_bottom - cal.y_top) < 400)
    return false;
  x_left_ = cal.x_left;
  x_right_ = cal.x_right;
  y_top_ = cal.y_top;
  y_bottom_ = cal.y_bottom;
  swap_ = cal.swap;
  return true;
}

void TouchPanel::save() {
  StoredCal cal = {};
  cal.magic = kCalMagic;
  cal.rotation = kPanelRotation;
  cal.swap = swap_;
  cal.x_left = x_left_;
  cal.x_right = x_right_;
  cal.y_top = y_top_;
  cal.y_bottom = y_bottom_;
  Preferences prefs;
  if (!prefs.begin("touch", false))
    return;
  prefs.putBytes("cal", &cal, sizeof(cal));
  prefs.end();
}

static void drawCross(TFT_eSPI &tft, int x, int y) {
  uint16_t fg = tft.color565(232, 236, 240);
  tft.drawFastHLine(x - 12, y, 25, fg);
  tft.drawFastVLine(x, y - 12, 25, fg);
  tft.fillCircle(x, y, 2, tft.color565(230, 80, 60));
}

bool TouchPanel::calibrate(TFT_eSPI &tft) {
  const int sx[4] = {20, PANEL_W - 21, PANEL_W - 21, 20};
  const int sy[4] = {20, 20, PANEL_H - 21, PANEL_H - 21};
  int rawX[4], rawY[4];
  uint16_t bg = tft.color565(16, 18, 22);

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(tft.color565(232, 236, 240), bg);
    tft.drawString("Touch the cross", PANEL_W / 2, PANEL_H / 2 - 12);
    tft.setTextFont(1);
    tft.setTextColor(tft.color565(150, 158, 168), bg);
    char step[16];
    snprintf(step, sizeof(step), "%d / 4    BOOT skips", i + 1);
    tft.drawString(step, PANEL_W / 2, PANEL_H / 2 + 14);
    drawCross(tft, sx[i], sy[i]);

    uint32_t start = millis();
    while (millis() - start < 20000) {
      if (digitalRead(BOOT_PIN) == LOW)
        return false;
      if (digitalRead(TP_PIN_IRQ) != LOW)
        break;
      delay(15);
    }

    bool got = false;
    while (millis() - start < 45000) {
      if (digitalRead(BOOT_PIN) == LOW)
        return false;
      int xs[8], ys[8], zs[8];
      int count = 0;
      for (int s = 0; s < 8; s++) {
        int x, y, z;
        if (!sample(x, y, z))
          break;
        xs[count] = x;
        ys[count] = y;
        zs[count] = z;
        count++;
        delay(6);
      }
      if (count < 6)
        continue;
      int minX = xs[0], maxX = xs[0], minY = ys[0], maxY = ys[0];
      long sumX = 0, sumY = 0;
      for (int s = 0; s < count; s++) {
        sumX += xs[s];
        sumY += ys[s];
        if (xs[s] < minX) minX = xs[s];
        if (xs[s] > maxX) maxX = xs[s];
        if (ys[s] < minY) minY = ys[s];
        if (ys[s] > maxY) maxY = ys[s];
      }
      if (maxX - minX > 90 || maxY - minY > 90)
        continue;
      rawX[i] = static_cast<int>(sumX / count);
      rawY[i] = static_cast<int>(sumY / count);
      got = true;
      break;
    }
    if (!got)
      return false;

    while (digitalRead(TP_PIN_IRQ) == LOW) {
      if (digitalRead(BOOT_PIN) == LOW)
        return false;
      delay(15);
    }
    delay(120);
  }

  int spanX = abs(rawX[1] - rawX[0]) + abs(rawX[2] - rawX[3]);
  int spanY = abs(rawY[1] - rawY[0]) + abs(rawY[2] - rawY[3]);
  swap_ = spanY > spanX ? 1 : 0;
  if (swap_) {
    for (int i = 0; i < 4; i++) {
      int t = rawX[i];
      rawX[i] = rawY[i];
      rawY[i] = t;
    }
  }

  x_left_ = static_cast<int16_t>((rawX[0] + rawX[3]) / 2);
  x_right_ = static_cast<int16_t>((rawX[1] + rawX[2]) / 2);
  y_top_ = static_cast<int16_t>((rawY[0] + rawY[1]) / 2);
  y_bottom_ = static_cast<int16_t>((rawY[2] + rawY[3]) / 2);
  if (abs(x_right_ - x_left_) < 400 || abs(y_bottom_ - y_top_) < 400)
    return false;

  valid_ = true;
  save();
  return true;
}

TouchSample TouchPanel::read() {
  TouchSample out = {false, 0, 0};
  if (!valid_)
    return out;

  int x, y, z;
  if (!sample(x, y, z))
    return out;

  int rx = swap_ ? y : x;
  int ry = swap_ ? x : y;
  int sx = map(rx, x_left_, x_right_, 0, PANEL_W - 1);
  int sy = map(ry, y_top_, y_bottom_, 0, PANEL_H - 1);
  out.down = true;
  out.x = static_cast<int16_t>(constrain(sx, 0, PANEL_W - 1));
  out.y = static_cast<int16_t>(constrain(sy, 0, PANEL_H - 1));
  return out;
}
