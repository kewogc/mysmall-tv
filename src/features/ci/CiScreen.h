// CiScreen.h — the build screen. While a CI build runs it takes the display
// from the active feature, the way a notification does; main.cpp gives the
// feature a repaint when it is over.
#pragma once
#include "config.h"
#if WITH_CI
// True while there is something to show. Call every loop turn: it also lets
// the timed views run out.
bool ciScreenActive();
// Draw what changed. `repaint` forces a full redraw (something else drew on
// the screen since the last call).
void ciScreenService(bool repaint);
// Something else drew on the screen: the next ciScreenService() redraws it all.
void ciScreenInvalidate();
#endif
