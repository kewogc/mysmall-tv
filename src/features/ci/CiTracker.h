// CiTracker.h — what the build screen shows, decided from poll results.
//
// The client (CiClient.cpp) asks the CI server which builds are running and
// hands over a CiSnapshot. The tracker turns a series of snapshots into one of
// four views:
//   SPLASH  a build seen for the first time fills the screen for a moment;
//   LIST    every running build, one row each, with its progress;
//   FAILED  a build that ended badly stays on the screen for a while;
//   IDLE    nothing to show, the active feature has the screen.
// A build that ended well stays in the list as done for a few seconds.
//
// Plain C++, no Arduino types: tests/ci runs it on the desktop.
#pragma once
#include <stdint.h>
#include <string.h>

namespace smalltv {

enum CiStatus : uint8_t { CI_PENDING, CI_RUNNING, CI_SUCCESS, CI_FAILED };

struct CiBuild {
  uint32_t id = 0;            // the server's build id
  uint32_t number = 0;        // the build number within its repository
  uint32_t started = 0;       // server clock, epoch seconds; 0 while it waits
  char repo[32] = {0};        // repository name without its namespace
  char slug[72] = {0};        // namespace/name
  char ref[32] = {0};         // tag or branch
  char author[24] = {0};      // who triggered it
  char avatar[128] = {0};     // URL of that person's picture
  char step[28] = {0};        // the step that is running, or the one that failed
  char machine[24] = {0};     // the runner
  uint8_t stepsDone = 0, stepsTotal = 0;
  CiStatus status = CI_PENDING;
};

constexpr int kCiMaxBuilds = 4;   // rows that fit on the screen

// One poll: the builds that run now, and those that left the list since the
// last poll with the status they ended in.
struct CiSnapshot {
  CiBuild running[kCiMaxBuilds];
  uint8_t runningCount = 0;
  uint8_t moreRunning = 0;      // running builds beyond kCiMaxBuilds
  CiBuild finished[kCiMaxBuilds];
  uint8_t finishedCount = 0;
};

class CiTracker {
 public:
  enum View : uint8_t { IDLE, SPLASH, LIST, FAILED };
  struct Row {
    CiBuild  build;
    uint32_t firstSeenMs = 0;
    uint32_t doneMs = 0;        // when it ended well; meaningful while `done`
    bool     done = false;
  };

  // A hold of 0 turns that view off.
  void configure(uint32_t splashMs, uint32_t failMs, uint32_t doneMs) {
    splashMs_ = splashMs; failMs_ = failMs; doneMs_ = doneMs;
  }
  void clear() {
    rowCount_ = splashCount_ = failCount_ = more_ = 0;
    splashStart_ = failStart_ = 0;
    splashShown_ = failShown_ = false;
    ++revision_;
  }

  void apply(const CiSnapshot& s, uint32_t nowMs) {
    bool changed = false;
    // Builds that ended.
    for (uint8_t i = 0; i < s.finishedCount; ++i) {
      const CiBuild& b = s.finished[i];
      int at = find(b.id);
      if (b.status == CI_FAILED && failMs_) {
        if (at >= 0) removeRow(at);
        dropSplash(b.id);
        push(fail_, failCount_, b);
      } else if (at >= 0) {
        Row& r = rows_[at];
        r.build.status = b.status;
        copy(r.build.step, b.step);
        if (b.status == CI_SUCCESS) r.build.stepsDone = r.build.stepsTotal = b.stepsTotal ? b.stepsTotal : r.build.stepsTotal;
        r.done = true;
        r.doneMs = nowMs;
      }
      changed = true;
    }
    // Builds that run.
    for (uint8_t i = 0; i < s.runningCount; ++i) {
      const CiBuild& b = s.running[i];
      int at = find(b.id);
      if (at < 0) {
        at = addRow(nowMs);
        if (at < 0) continue;            // no room: the list is full of running builds
        rows_[at].build = b;
        if (splashMs_) push(splash_, splashCount_, b);
        changed = true;
        continue;
      }
      Row& r = rows_[at];
      if (r.done) { r.done = false; changed = true; }   // restarted under the same id
      if (differs(r.build, b)) { r.build = b; changed = true; }
    }
    // Rows the server no longer reports and gave no result for: gone.
    for (int i = rowCount_ - 1; i >= 0; --i) {
      if (rows_[i].done) continue;
      bool listed = false;
      for (uint8_t k = 0; k < s.runningCount; ++k) if (s.running[k].id == rows_[i].build.id) listed = true;
      if (!listed) { dropSplash(rows_[i].build.id); removeRow(i); changed = true; }
    }
    if (more_ != s.moreRunning) { more_ = s.moreRunning; changed = true; }
    if (changed) ++revision_;
  }

  // The view at `nowMs`. Also lets the timed views run out, so call it every
  // loop turn. Millisecond arithmetic survives the 32-bit wrap.
  View view(uint32_t nowMs) {
    for (int i = rowCount_ - 1; i >= 0; --i)
      if (rows_[i].done && nowMs - rows_[i].doneMs >= doneMs_) { removeRow(i); ++revision_; }
    while (failCount_) {
      if (!failShown_) { failShown_ = true; failStart_ = nowMs; ++revision_; }
      if (nowMs - failStart_ < failMs_) return FAILED;
      pop(fail_, failCount_);
      failShown_ = false;
      ++revision_;
    }
    while (splashCount_) {
      if (!splashShown_) { splashShown_ = true; splashStart_ = nowMs; ++revision_; }
      if (nowMs - splashStart_ < splashMs_) return SPLASH;
      pop(splash_, splashCount_);
      splashShown_ = false;
      ++revision_;
    }
    return rowCount_ ? LIST : IDLE;
  }

  // SPLASH and FAILED: the build on the screen.
  const CiBuild& focus() const { return failCount_ ? fail_[0] : splash_[0]; }
  int rows() const { return rowCount_; }
  const Row& row(int i) const { return rows_[i]; }
  uint8_t more() const { return more_; }
  // Running builds, for the next poll to ask about by name.
  int runningBuilds(CiBuild* out) const {
    int n = 0;
    for (int i = 0; i < rowCount_; ++i) if (!rows_[i].done) out[n++] = rows_[i].build;
    return n;
  }
  // Goes up whenever what is on the screen changed; a renderer repaints then.
  uint32_t revision() const { return revision_; }

 private:
  static void copy(char* dst, const char* src) { memcpy(dst, src, sizeof(CiBuild::step)); }
  static bool differs(const CiBuild& a, const CiBuild& b) {
    return a.status != b.status || a.stepsDone != b.stepsDone || a.stepsTotal != b.stepsTotal ||
           a.started != b.started || strcmp(a.step, b.step) || strcmp(a.machine, b.machine);
  }
  int find(uint32_t id) const {
    for (int i = 0; i < rowCount_; ++i) if (rows_[i].build.id == id) return i;
    return -1;
  }
  // A free row: a new one, or the place of the oldest finished build.
  int addRow(uint32_t nowMs) {
    if (rowCount_ == kCiMaxBuilds) {
      int oldest = -1;
      for (int i = 0; i < rowCount_; ++i)
        if (rows_[i].done && (oldest < 0 || nowMs - rows_[i].doneMs > nowMs - rows_[oldest].doneMs)) oldest = i;
      if (oldest < 0) return -1;
      removeRow(oldest);
    }
    rows_[rowCount_] = Row();
    rows_[rowCount_].firstSeenMs = nowMs;
    return rowCount_++;
  }
  void removeRow(int at) {
    for (int i = at; i + 1 < rowCount_; ++i) rows_[i] = rows_[i + 1];
    --rowCount_;
  }
  static void push(CiBuild* queue, uint8_t& count, const CiBuild& b) {
    for (uint8_t i = 0; i < count; ++i) if (queue[i].id == b.id) { queue[i] = b; return; }
    if (count < kCiMaxBuilds) queue[count++] = b;
  }
  static void pop(CiBuild* queue, uint8_t& count) {
    for (uint8_t i = 0; i + 1 < count; ++i) queue[i] = queue[i + 1];
    --count;
  }
  // A build that is gone before its splash came up does not get one. The one
  // on the screen stays for its time.
  void dropSplash(uint32_t id) {
    for (uint8_t i = splashShown_ ? 1 : 0; i < splashCount_; ++i)
      if (splash_[i].id == id) {
        for (uint8_t k = i; k + 1 < splashCount_; ++k) splash_[k] = splash_[k + 1];
        --splashCount_;
        return;
      }
  }

  Row      rows_[kCiMaxBuilds];
  CiBuild  splash_[kCiMaxBuilds], fail_[kCiMaxBuilds];
  uint8_t  rowCount_ = 0, splashCount_ = 0, failCount_ = 0, more_ = 0;
  bool     splashShown_ = false, failShown_ = false;
  uint32_t splashStart_ = 0, failStart_ = 0;
  uint32_t splashMs_ = 4000, failMs_ = 10000, doneMs_ = 5000;
  uint32_t revision_ = 0;
};

}  // namespace smalltv
