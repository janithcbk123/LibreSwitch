// =====================================================================
//  ports/indicator_sink.h — outbound port (Core Domain → LED hardware)
// ---------------------------------------------------------------------
//  The IndicatorController drives the status LED + backlight THROUGH
//  this port; a platform adapter implements it (GPIO22/GPIO23). PWM is a
//  capability the adapter advertises (Phase 1 A-3: PWM unverified →
//  default OFF). When PWM is unavailable, the controller/adapter falls
//  back to ON/OFF — brightness is simply ignored. Keeps the controller
//  pure and host-testable.
// =====================================================================
#pragma once

#include "../domain/indicator_types.h"

namespace ss {

class IndicatorSink {
public:
    virtual ~IndicatorSink() = default;

    // Status LED (GPIO22): simple on/off — patterns are produced by the
    // controller toggling this over time.
    virtual void setStatusLed(bool on) = 0;

    // Backlight (GPIO23): on/off always supported. If the profile is
    // PWM-capable AND on==true, brightness (0..100) is applied; otherwise
    // brightness is ignored and it's plain on/off.
    virtual void setBacklight(bool on, uint8_t brightness) = 0;

    // Does this build/profile actually support PWM brightness? (Phase 1
    // A-3 capability flag.) The controller uses this to decide whether to
    // bother varying brightness or just treat backlight as on/off.
    virtual bool pwmCapable() const = 0;
};

}  // namespace ss
