// =====================================================================
//  platform/clock.h — monotonic clock seam (+ LibreTiny impl)
// ---------------------------------------------------------------------
//  The domain is time-injected (TouchInput, IndicatorController take
//  nowMs); this seam supplies it. Host tests pass synthetic time; on
//  device the impl reads millis(). Keeping it behind a seam means the
//  composition root is the only place that knows about millis().
// =====================================================================
#pragma once

#include "../domain/touch_types.h"   // Millis

namespace ss {

class Clock {
public:
    virtual ~Clock() = default;
    virtual Millis nowMs() = 0;
};

}  // namespace ss

#if defined(LT_BUILD) || defined(ARDUINO)
#include <Arduino.h>
namespace ss {
class ArduinoClock final : public Clock {
public:
    Millis nowMs() override { return static_cast<Millis>(millis()); }
};
}  // namespace ss
#endif
