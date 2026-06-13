// =====================================================================
//  adapters/gpio_indicator_sink.h — GpioIndicatorSink (outbound adapter)
// ---------------------------------------------------------------------
//  Implements the domain IndicatorSink: status LED (GPIO22) on/off and
//  backlight (GPIO23) on/off, mapping pins via the active DeviceProfile.
//
//  PWM FINDING (resolves Phase 1 A-3, previously "PWM unverified"):
//  On the reference BK7231N variant the PWM channels are GPIO 6,7,8,9,
//  24,26 — ALL consumed by relays (6,8,9,26) and touch (7,24). The
//  backlight pin GPIO23 is PIN_ADC3, NOT a PWM output. Therefore PWM
//  backlight brightness is NOT available on this hardware. The adapter
//  honestly reports pwmCapable()=false; backlight is plain on/off.
//  (R-32: documented hardware limitation, not a software gap.)
//
//  The seam still supports PWM via analogWrite for FUTURE profiles whose
//  backlight lands on a PWM-capable pin — pwmCapable is profile-driven,
//  not hardcoded, so no logic changes when better hardware appears.
//
//  Status-LED brightness is irrelevant (it's a status indicator, on/off).
//  Logic host-testable via mock GpioHal; LibreTiny calls are on-device.
// =====================================================================
#pragma once

#include "../ports/indicator_sink.h"
#include "../platform/gpio_hal.h"
#include "../profiles/device_profile.h"
#include "../domain/result.h"

namespace ss {

// LED/backlight drive polarity (same rationale as relays — many boards
// sink the LED, i.e. active-low). Explicit, not assumed.
enum class IndicatorPolarity : uint8_t {
    ActiveHigh = 0,
    ActiveLow  = 1,
};

class GpioIndicatorSink final : public IndicatorSink {
public:
    GpioIndicatorSink(GpioHal& hal, const DeviceProfile& profile,
                      IndicatorPolarity polarity = IndicatorPolarity::ActiveHigh)
        : hal_(hal), profile_(profile), polarity_(polarity) {}

    // Configure LED + backlight pins as outputs, both driven OFF.
    Status begin() {
        hal_.configureOutput(profile_.statusLedPin);
        driveLevel(profile_.statusLedPin, false);
        if (profile_.backlightPin != kNoPin) {
            hal_.configureOutput(profile_.backlightPin);
            driveLevel(profile_.backlightPin, false);
        }
        return Status::success();
    }

    // ---- IndicatorSink ----
    void setStatusLed(bool on) override {
        driveLevel(profile_.statusLedPin, on);
    }

    void setBacklight(bool on, uint8_t /*brightness*/) override {
        // On this hardware backlight is on/off only (see PWM FINDING).
        // brightness is intentionally ignored here; pwmCapable()==false
        // tells the controller not to bother varying it.
        if (profile_.backlightPin == kNoPin) return;
        driveLevel(profile_.backlightPin, on);
    }

    // Profile-driven, honest answer. False on the reference HW (R-32).
    bool pwmCapable() const override { return profile_.pwmCapable; }

private:
    // Apply polarity in one place: logical "on" → physical pin level.
    void driveLevel(uint8_t pin, bool on) {
        const bool pinHigh = (polarity_ == IndicatorPolarity::ActiveHigh) ? on : !on;
        hal_.writePin(pin, pinHigh);
    }

    GpioHal&             hal_;
    const DeviceProfile& profile_;
    IndicatorPolarity    polarity_;
};

}  // namespace ss
