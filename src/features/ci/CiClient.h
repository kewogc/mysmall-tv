// CiClient.h — polls the CI server and keeps the pictures of who started a build.
//
// ciService() runs from the main loop. It starts a poll in a background task
// when one is due, and feeds the task's result to the tracker (CiTracker.h),
// which CiScreen.cpp draws from.
#pragma once
#include "config.h"
#if WITH_CI
#include "Settings.h"
#include "CiTracker.h"

void ciBegin(const Settings& s);
// The settings changed: forget what is on the screen and poll afresh.
void ciApplySettings(const Settings& s);
void ciService(const Settings& s);
smalltv::CiTracker& ciTracker();

// Run the screens once with made-up builds, about half a minute. With an
// `avatarUrl` the builds carry that picture, fetched like a real one; without,
// the demo needs no network.
void ciStartDemo(const char* avatarUrl);

// For the web UI.
struct CiState {
  bool     enabled;
  bool     demo;
  bool     polled;       // at least one poll has come back
  bool     ok;           // the last poll worked
  char     error[48];    // why it did not
  uint32_t lastPollSec;  // seconds since that poll came back
  uint8_t  running;      // builds in the list
};
CiState ciState(const Settings& s);

// ---- avatars --------------------------------------------------------------
// The picture behind a build's avatar URL, scaled to a square of
// CI_AVATAR_BIG or CI_AVATAR_SMALL pixels, RGB565; nullptr until it has been
// fetched (the poll task downloads it and keeps it in flash under /ci).
#define CI_AVATAR_BIG   96
#define CI_AVATAR_SMALL 24
const uint16_t* ciAvatarBig(const char* url);
const uint16_t* ciAvatarSmall(const char* url);
// Goes up when a new picture has arrived; the screen repaints then.
uint32_t ciAvatarRevision();
// Give back the RAM the pictures hold (the screen is idle).
void ciAvatarRelease();
#endif
