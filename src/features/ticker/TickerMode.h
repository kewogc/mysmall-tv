// TickerMode.h — stock/crypto ticker feature.
//
// Rotates through the configured symbols, showing price, change and a sparkline.
// Owns its fetch (StockClient), its rendering, and its rotation/dirty state.
#pragma once
#include "Mode.h"
#include "config.h"

class TickerMode : public DisplayMode {
 public:
  TickerMode(uint8_t setIdx = 0, uint8_t modeId = MODE_STOCKS, const char* name = "stocks")
      : setIdx_(setIdx), modeId_(modeId), name_(name) {}

  const char* id() const override { return name_; }
  uint8_t     modeConst() const override { return modeId_; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  void render(const Settings& s);

  uint8_t  setIdx_ = 0;
  uint8_t  modeId_ = MODE_STOCKS;
  const char* name_ = "stocks";
  uint8_t  curPage_ = 0;
  uint32_t lastRotate_ = 0;
  uint32_t renderedLastOk_ = 0xFFFFFFFF;
  bool     renderedError_ = false;
  bool     needRender_ = true;
};

extern TickerMode g_tickerMode;
extern TickerMode g_tickerMode2;
