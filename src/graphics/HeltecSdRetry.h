#pragma once

#include "mesh/Throttle.h"
#include <Arduino.h>

class HeltecSdRetry
{
  public:
    bool ready() const { return !failed || !Throttle::isWithinTimespanMs(failedAt, 10000); }
    void failure()
    {
        failedAt = millis();
        failed = true;
    }
    void success() { failed = false; }

  private:
    uint32_t failedAt = 0;
    bool failed = false;
};
