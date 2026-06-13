// =====================================================================
//  platform/gpio_hal_libretiny.h — LibreTiny GpioHal impl (ON-DEVICE ONLY)
// ---------------------------------------------------------------------
//  The ONE place that touches LibreTiny GPIO. Compiled only for the
//  device target (guarded by LT_BUILD), never in host tests — so the
//  host suite never needs an Arduino mock for this. Its correctness is
//  proven on-device, not in CI (the honest boundary, like R-0).
//
//  Verified API: pinMode(pin_size_t, PinMode) / digitalWrite(pin_size_t,
//  PinStatus); OUTPUT/HIGH/LOW standard constants; raw GPIO pin numbers.
// =====================================================================
#pragma once

#include "gpio_hal.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <Arduino.h>

namespace ss {

class LibreTinyGpioHal final : public GpioHal {
public:
    void configureOutput(uint8_t pin) override {
        pinMode(static_cast<pin_size_t>(pin), OUTPUT);
    }
    void writePin(uint8_t pin, bool high) override {
        digitalWrite(static_cast<pin_size_t>(pin), high ? HIGH : LOW);
    }
    void configureInput(uint8_t pin, bool pullUp) override {
        pinMode(static_cast<pin_size_t>(pin), pullUp ? INPUT_PULLUP : INPUT);
    }
    bool readPin(uint8_t pin) override {
        return digitalRead(static_cast<pin_size_t>(pin)) == HIGH;
    }
};

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
