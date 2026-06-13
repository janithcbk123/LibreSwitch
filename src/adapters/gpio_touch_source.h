// =====================================================================
//  adapters/gpio_touch_source.h — GpioTouchSource (inbound-edge adapter)
// ---------------------------------------------------------------------
//  Reads the capacitive touch pads through the GpioHal seam, maps pins
//  via the active DeviceProfile, and feeds raw levels into the domain's
//  TouchInput engine each Control-task cycle. It is a THIN reader: all
//  debounce / long-press / factory-reset-gesture intelligence lives in
//  the already-tested TouchInput domain component — this adapter only
//  turns pins into TouchLevel and calls poll().
//
//  HW note (R-31): touch pins GPIO20/GPIO14 share silicon function with
//  WIRE1_SCL / SPI0_SCK. They work as plain GPIO inputs ONLY because we
//  do NOT enable I2C1 or SPI0. Enabling either would steal these pins.
//
//  Logic (mapping, active-level, scan loop) is host-testable with a mock
//  GpioHal; only the LibreTiny HAL impl needs the device.
// =====================================================================
#pragma once

#include "../domain/touch_input.h"
#include "../platform/gpio_hal.h"
#include "../profiles/device_profile.h"
#include "../domain/result.h"

namespace ss {

// Logic level that indicates a finger present. Capacitive pad modules
// differ; make it explicit rather than assume (mirrors relay polarity).
enum class TouchActiveLevel : uint8_t {
    ActiveHigh = 0,   // pad pressed → pin reads HIGH
    ActiveLow  = 1,   // pad pressed → pin reads LOW
};

class GpioTouchSource {
public:
    GpioTouchSource(GpioHal& hal, const DeviceProfile& profile,
                    TouchInput& engine,
                    TouchActiveLevel active = TouchActiveLevel::ActiveHigh)
        : hal_(hal), profile_(profile), engine_(engine), active_(active) {}

    // Configure each present touch pin as an input. Pull direction is the
    // complement of the active level (active-high pad → pull-down idle;
    // active-low pad → pull-up idle) so the idle reading is unambiguous.
    Status begin() {
        const bool pullUp = (active_ == TouchActiveLevel::ActiveLow);
        for (ChannelId ch = 0; ch < profile_.channelCount; ++ch) {
            const uint8_t pin = profile_.channels[ch].touchPin;
            if (pin == kNoPin) return Status::fail(Error::InvalidArgument);
            hal_.configureInput(pin, pullUp);
        }
        return Status::success();
    }

    // Read all present channels and feed the engine. Called once per
    // Control-task poll cycle with the current monotonic time.
    Status scan(Millis nowMs) {
        for (ChannelId ch = 0; ch < profile_.channelCount; ++ch) {
            const uint8_t pin = profile_.channels[ch].touchPin;
            if (pin == kNoPin) continue;
            engine_.poll(ch, readLevel(pin), nowMs);
        }
        return Status::success();
    }

private:
    // Map a raw pin read to a debounce-engine TouchLevel, applying the
    // pad's active-level convention in one place.
    TouchLevel readLevel(uint8_t pin) {
        const bool high = hal_.readPin(pin);
        const bool pressed = (active_ == TouchActiveLevel::ActiveHigh) ? high : !high;
        return pressed ? TouchLevel::Pressed : TouchLevel::Released;
    }

    GpioHal&             hal_;
    const DeviceProfile& profile_;
    TouchInput&          engine_;
    TouchActiveLevel     active_;
};

}  // namespace ss
