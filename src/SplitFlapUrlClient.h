#pragma once

#include "JsonSettings.h"

#include <Arduino.h>
#include <time.h>

class SplitFlapUrlClient {
  public:
    explicit SplitFlapUrlClient(JsonSettings &settings);

    bool fetchIfDue(String &content);
    void requestRefresh() { refreshRequested = true; }

  private:
    static constexpr unsigned long FETCH_INTERVAL_MS = 60UL * 1000UL;

    JsonSettings &settings;
    unsigned long lastFetchTime = 0;
    time_t lastFetchMinute = -1;
    bool refreshRequested = true;
};
