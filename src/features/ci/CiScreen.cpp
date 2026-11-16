// CiScreen.cpp — draws the build screen from the tracker's view.
//
//   SPLASH  who started which build: the person's picture in the middle, the
//           repository under it, then the tag or branch and the name.
//   LIST    one row per build: small picture, repository, running time, the
//           step with "done/total", and a progress bar.
//   FAILED  like the splash, in red, with the step that failed.
#include "CiScreen.h"
#if WITH_CI
#include "CiClient.h"
#include "Gfx.h"
#include <Arduino_GFX_Library.h>
#include <time.h>

using namespace smalltv;

namespace {

const int kRowTop = 26, kRowH = 52;          // list geometry
const int kTimeW = 5 * GFX_FONT_W * 2;       // "mm:ss" at size 2

CiTracker::View g_view = CiTracker::IDLE;
uint32_t g_revision = 0;       // tracker + avatar revision the screen shows
uint32_t g_second = 0;         // millis()/1000 of the last timer repaint
bool     g_painted = false;

// `text` cut to `cells` character cells. Names are ASCII; a cut inside a
// two-byte letter would only lose that letter.
const char* fit(const char* text, int cells, char* buffer, size_t size) {
  size_t n = 0;
  for (; text[n] && n + 1 < size && int(n) < cells; ++n) buffer[n] = text[n];
  buffer[n] = 0;
  return buffer;
}

// Seconds a build has been running. The server's start time against our
// clock when the clock is set, else the time since we first saw the build.
uint32_t elapsedSec(const CiBuild& b, uint32_t firstSeenMs) {
  const time_t now = time(nullptr);
  if (now > 1600000000 && b.started) return now > time_t(b.started) ? uint32_t(now - b.started) : 0;
  return (millis() - firstSeenMs) / 1000;
}

void formatTime(uint32_t sec, char* out, size_t size) {
  if (sec >= 6000) snprintf(out, size, "%3luh", (unsigned long)(sec / 3600));
  else snprintf(out, size, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

// A colour for a name, so two people without pictures can be told apart.
uint16_t nameColor(const char* name) {
  static const uint16_t colors[] = {0x041F, 0x05E0, 0xFC00, 0xA11F, 0x07FF, 0xF81F};
  uint8_t h = 0;
  for (; *name; ++name) h = uint8_t(h * 31 + uint8_t(*name));
  return gfxTint(colors[h % (sizeof(colors) / sizeof(colors[0]))]);
}

// The person's picture, or a disc with the first letter of the name.
void drawAvatar(Arduino_GFX* gfx, const CiBuild& b, int x, int y, bool big) {
  const int size = big ? CI_AVATAR_BIG : CI_AVATAR_SMALL;
  const uint16_t* px = big ? ciAvatarBig(b.avatar) : ciAvatarSmall(b.avatar);
  if (px) {
    gfx->draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(px), size, size);
    return;
  }
  const int r = size / 2;
  gfx->fillCircle(x + r, y + r, r, nameColor(b.author));
  char letter[2] = {char(b.author[0] ? toupper(b.author[0]) : '?'), 0};
  const uint8_t scale = big ? 6 : 2;
  gfx->setTextSize(scale);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(x + r - GFX_FONT_W * scale / 2 + scale / 2, y + r - GFX_FONT_H * scale / 2 + scale / 2);
  gfx->print(letter);
}

// The screens that show one build: its picture, name and what happened.
void drawFocus(Arduino_GFX* gfx, const CiBuild& b, bool failed) {
  gfx->fillScreen(C_BLACK);
  const uint16_t accent = failed ? C_RED : C_CYAN;
  gfxDrawCentered(failed ? TR("BUILD FAILED", "СБОРКА УПАЛА") : TR("BUILD", "СБОРКА"), 6, 2, accent);
  const int ax = (TFT_WIDTH - CI_AVATAR_BIG) / 2, ay = 30;
  drawAvatar(gfx, b, ax, ay, true);
  gfx->drawRect(ax - 2, ay - 2, CI_AVATAR_BIG + 4, CI_AVATAR_BIG + 4, accent);

  const char* repo = b.repo[0] ? b.repo : "?";
  gfxDrawCentered(repo, 136, gfxFitSize(repo, 232, 3), C_WHITE);

  char line[48];
  if (failed && b.step[0]) {
    gfxDrawCentered(b.step, 166, gfxFitSize(b.step, 232, 2), C_RED);
  } else if (b.ref[0]) {
    gfxDrawCentered(b.ref, 166, gfxFitSize(b.ref, 232, 2), C_YELLOW);
  }
  if (b.author[0]) gfxDrawCentered(b.author, 190, gfxFitSize(b.author, 232, 2), C_GRAY);
  if (failed && b.ref[0]) snprintf(line, sizeof(line), "#%lu %s", (unsigned long)b.number, b.ref);
  else if (b.machine[0])  snprintf(line, sizeof(line), "#%lu %s", (unsigned long)b.number, b.machine);
  else                    snprintf(line, sizeof(line), "#%lu", (unsigned long)b.number);
  gfxDrawCentered(line, 214, gfxFitSize(line, 232, 2), C_DGRAY);
}

void drawTime(Arduino_GFX* gfx, const CiTracker::Row& row, int y) {
  uint32_t sec = elapsedSec(row.build, row.firstSeenMs);
  if (row.done) {   // stopped when it ended
    const uint32_t since = (millis() - row.doneMs) / 1000;
    sec = sec > since ? sec - since : 0;
  }
  char text[8];
  formatTime(sec, text, sizeof(text));
  gfx->setTextSize(2);
  gfx->setTextColor(row.done ? C_GRAY : C_WHITE, C_BLACK);   // with a background: no flicker
  gfx->setCursor(TFT_WIDTH - 6 - kTimeW, y + 4);
  gfx->print(text);
}

void drawRow(Arduino_GFX* gfx, const CiTracker::Row& row, int y) {
  const CiBuild& b = row.build;
  drawAvatar(gfx, b, 6, y + 2, false);

  char text[40];
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(36, y + 4);
  gfx->print(fit(b.repo[0] ? b.repo : "?", (TFT_WIDTH - 36 - kTimeW - 10) / (GFX_FONT_W * 2), text, sizeof(text)));
  drawTime(gfx, row, y);

  // The step and how far the build is, or how it ended.
  uint16_t color = C_GRAY;
  if (row.done && b.status == CI_SUCCESS) {
    snprintf(text, sizeof(text), "%s", TR("done", "готово"));
    color = C_GREEN;
  } else if (row.done) {
    snprintf(text, sizeof(text), "%s %s", TR("failed:", "ошибка:"), b.step);
    color = C_RED;
  } else if (b.status == CI_PENDING) {
    snprintf(text, sizeof(text), "%s", TR("waiting for a runner", "ждёт исполнителя"));
  } else {
    char step[24];
    snprintf(text, sizeof(text), "%s %u/%u", fit(b.step[0] ? b.step : "...", 22, step, sizeof(step)),
             unsigned(b.stepsDone), unsigned(b.stepsTotal));
  }
  gfx->setTextSize(1);
  gfx->setTextColor(color);
  gfx->setCursor(36, y + 24);
  gfx->print(text);

  const int bx = 6, bw = TFT_WIDTH - 12, by = y + 38, bh = 6;
  gfx->fillRect(bx, by, bw, bh, C_DGRAY);
  int filled = b.stepsTotal ? bw * b.stepsDone / b.stepsTotal : 0;
  if (row.done && b.status == CI_SUCCESS) filled = bw;
  if (filled > 0) gfx->fillRect(bx, by, filled, bh, row.done ? (b.status == CI_SUCCESS ? C_GREEN : C_RED) : C_CYAN);
}

void drawList(Arduino_GFX* gfx, const CiTracker& t) {
  gfx->fillScreen(C_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(C_CYAN);
  gfx->setCursor(6, 5);
  gfx->print(TR("BUILDS", "СБОРКИ"));
  char count[12];
  if (t.more()) snprintf(count, sizeof(count), "%d+%u", t.rows(), unsigned(t.more()));
  else          snprintf(count, sizeof(count), "%d", t.rows());
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(TFT_WIDTH - 6 - gfxTextW(count, 2), 5);
  gfx->print(count);
  gfx->drawFastHLine(6, 23, TFT_WIDTH - 12, C_DGRAY);
  for (int i = 0; i < t.rows(); ++i) drawRow(gfx, t.row(i), kRowTop + i * kRowH);
}

}  // namespace

bool ciScreenActive() {
  const bool active = ciTracker().view(millis()) != CiTracker::IDLE;
  if (!active && g_painted) {     // the screen goes back to the feature
    g_painted = false;
    g_view = CiTracker::IDLE;
    ciAvatarRelease();
  }
  return active;
}

void ciScreenInvalidate() { g_painted = false; }

void ciScreenService(bool repaint) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  CiTracker& t = ciTracker();
  const CiTracker::View view = t.view(millis());
  if (view == CiTracker::IDLE) return;
  const uint32_t revision = t.revision() + ciAvatarRevision();
  const uint32_t second = millis() / 1000;

  if (repaint || !g_painted || view != g_view || revision != g_revision) {
    if (view == CiTracker::LIST) drawList(gfx, t);
    else drawFocus(gfx, t.focus(), view == CiTracker::FAILED);
    g_view = view;
    g_revision = revision;
    g_second = second;
    g_painted = true;
    return;
  }
  // Nothing changed but the clock: only the running times move.
  if (view == CiTracker::LIST && second != g_second) {
    g_second = second;
    for (int i = 0; i < t.rows(); ++i)
      if (!t.row(i).done) drawTime(gfx, t.row(i), kRowTop + i * kRowH);
  }
}
#endif
