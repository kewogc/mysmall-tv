// The build screen's state machine (src/features/ci/CiTracker.h): a new build
// gets a splash, running builds make a list, a failure holds the screen, a
// success stays in the list for a moment, and then the screen is given back.
#include "CiTracker.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace smalltv;

static CiBuild build(uint32_t id, const char* repo, CiStatus status = CI_RUNNING, uint8_t done = 0, uint8_t total = 10) {
  CiBuild b;
  b.id = id; b.number = id; b.status = status; b.stepsDone = done; b.stepsTotal = total; b.started = 1000;
  snprintf(b.repo, sizeof(b.repo), "%s", repo);
  return b;
}
static CiSnapshot snap(std::initializer_list<CiBuild> running, std::initializer_list<CiBuild> finished = {}) {
  CiSnapshot s;
  for (const CiBuild& b : running) s.running[s.runningCount++] = b;
  for (const CiBuild& b : finished) s.finished[s.finishedCount++] = b;
  return s;
}

int main() {
  typedef CiTracker T;
  {  // One build from start to a good end.
    T t;
    t.configure(4000, 10000, 5000);
    assert(t.view(0) == T::IDLE);
    t.apply(snap({build(1, "api")}), 100);
    assert(t.view(100) == T::SPLASH && t.focus().id == 1);
    assert(t.view(4099) == T::SPLASH);
    assert(t.view(4100) == T::LIST && t.rows() == 1 && !t.row(0).done);
    uint32_t rev = t.revision();
    t.apply(snap({build(1, "api")}), 5000);                 // nothing new: no repaint
    assert(t.revision() == rev);
    t.apply(snap({build(1, "api", CI_RUNNING, 4)}), 6000);  // progress: repaint
    assert(t.revision() != rev && t.row(0).build.stepsDone == 4);
    t.apply(snap({}, {build(1, "api", CI_SUCCESS, 10)}), 9000);
    assert(t.view(9000) == T::LIST && t.row(0).done && t.row(0).build.stepsDone == 10);
    assert(t.view(13999) == T::LIST);
    assert(t.view(14000) == T::IDLE && t.rows() == 0);
  }
  {  // A failure takes the screen for its hold, then the rest of the list is back.
    T t;
    t.configure(4000, 10000, 5000);
    t.apply(snap({build(1, "api"), build(2, "web")}), 0);
    assert(t.view(0) == T::SPLASH && t.focus().id == 1);
    assert(t.view(4000) == T::SPLASH && t.focus().id == 2);   // one splash after the other
    assert(t.view(8000) == T::LIST && t.rows() == 2);
    CiBuild failed = build(2, "web", CI_FAILED, 3);
    snprintf(failed.step, sizeof(failed.step), "docker-build");
    t.apply(snap({build(1, "api")}, {failed}), 9000);
    assert(t.view(9000) == T::FAILED && t.focus().id == 2 && !strcmp(t.focus().step, "docker-build"));
    assert(t.rows() == 1 && t.row(0).build.id == 1);          // the failed one left the list
    assert(t.view(18999) == T::FAILED);
    assert(t.view(19000) == T::LIST);
    t.apply(snap({}, {build(1, "api", CI_SUCCESS)}), 20000);
    assert(t.view(25000) == T::IDLE);
  }
  {  // A failure outranks a splash that is waiting; the splash comes after it.
    T t;
    t.configure(4000, 10000, 5000);
    t.apply(snap({build(1, "api")}), 0);
    assert(t.view(0) == T::SPLASH);            // a hold starts when the view first comes up
    assert(t.view(4000) == T::LIST);
    t.apply(snap({build(3, "new")}, {build(1, "api", CI_FAILED)}), 5000);
    assert(t.view(5000) == T::FAILED && t.focus().id == 1);
    assert(t.view(15000) == T::SPLASH && t.focus().id == 3);
    assert(t.view(19000) == T::LIST && t.rows() == 1);
  }
  {  // Holds of zero: no splash, a failure stays in the list like a success.
    T t;
    t.configure(0, 0, 5000);
    t.apply(snap({build(1, "api")}), 0);
    assert(t.view(0) == T::LIST);
    t.apply(snap({}, {build(1, "api", CI_FAILED)}), 1000);
    assert(t.view(1000) == T::LIST && t.row(0).done && t.row(0).build.status == CI_FAILED);
    assert(t.view(6000) == T::IDLE);
  }
  {  // A build that vanished with no result is dropped, and so is its waiting splash.
    T t;
    t.configure(4000, 10000, 5000);
    t.apply(snap({build(1, "api"), build(2, "web")}), 0);
    assert(t.view(0) == T::SPLASH && t.focus().id == 1);
    t.apply(snap({build(1, "api")}), 1000);                    // 2 is gone before its splash
    assert(t.view(4000) == T::LIST && t.rows() == 1);
    t.apply(snap({}), 5000);
    assert(t.view(5000) == T::IDLE);
  }
  {  // Four rows at most; a fifth build takes the place of a finished one.
    T t;
    t.configure(0, 0, 60000);
    t.apply(snap({build(1, "a"), build(2, "b"), build(3, "c"), build(4, "d")}), 0);
    CiSnapshot s = snap({build(1, "a"), build(2, "b"), build(3, "c"), build(4, "d")});
    s.moreRunning = 2;
    t.apply(s, 100);
    assert(t.rows() == 4 && t.more() == 2);
    t.apply(snap({build(1, "a"), build(3, "c"), build(4, "d"), build(5, "e")}, {build(2, "b", CI_SUCCESS)}), 200);
    assert(t.rows() == 4);
    bool has5 = false, has2 = false;
    for (int i = 0; i < t.rows(); ++i) { has5 |= t.row(i).build.id == 5; has2 |= t.row(i).build.id == 2; }
    assert(has5 && !has2);
    CiBuild out[kCiMaxBuilds];
    assert(t.runningBuilds(out) == 4);
  }
  {  // The millisecond clock wraps.
    T t;
    t.configure(4000, 10000, 5000);
    const uint32_t near = 0xFFFFFFFFu - 1000;
    t.apply(snap({build(1, "api")}), near);
    assert(t.view(near) == T::SPLASH);
    assert(t.view(near + 3999) == T::SPLASH);     // past the wrap
    assert(t.view(near + 4000) == T::LIST);
  }
  {  // clear() gives the screen back at once.
    T t;
    t.configure(4000, 10000, 5000);
    t.apply(snap({build(1, "api")}), 0);
    t.clear();
    assert(t.view(10) == T::IDLE);
  }
  puts("ci tracker tests passed");
}
