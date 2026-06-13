// =====================================================================
//  platform/gpio_hal.h — GPIO HAL seam (Platform layer)
// ---------------------------------------------------------------------
//  The single thin seam between adapters and LibreTiny's GPIO. Isolating
//  pinMode/digitalWrite behind this interface means adapter LOGIC (pin
//  mapping, polarity, sequencing) is host-testable with a mock, and only
//  the one concrete LibreTiny implementation needs on-device proof. This
//  is the discipline that kept the domain pure; we extend it to adapters.
//
//  Verified against LibreTiny beken wiring_digital.c:
//    void pinMode(pin_size_t, PinMode);  void digitalWrite(pin_size_t, PinStatus);
//  GPIO numbers are raw pin ids (GPIO6 == 6). OUTPUT/HIGH/LOW are the
//  standard Arduino constants.
// =====================================================================
#pragma once

#include <cstdint>

namespace ss {

// Abstract GPIO operations the platform needs. Kept minimal — only what
// adapters actually use. Pin ids are raw GPIO numbers (per the variant).
class GpioHal {
public:
    virtual ~GpioHal() = default;
    virtual void configureOutput(uint8_t pin) = 0;   // pinMode(pin, OUTPUT)
    virtual void writePin(uint8_t pin, bool high) = 0;  // digitalWrite(pin, HIGH/LOW)

    // Input side (touch pads). configureInput selects pull mode; readPin
    // returns the current logic level. Capacitive touch boards vary in
    // idle level + pull needs, so pull is explicit (see TouchActiveLevel).
    virtual void configureInput(uint8_t pin, bool pullUp) = 0;  // pinMode INPUT[_PULLUP]
    virtual bool readPin(uint8_t pin) = 0;                      // digitalRead -> HIGH?
};

}  // namespace ss
