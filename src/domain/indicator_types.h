// =====================================================================
//  domain/indicator_types.h — indicator value types (Core Domain)
// ---------------------------------------------------------------------
//  Phase 1 A-3: status LED (GPIO22) + backlight (GPIO23). The domain
//  expresses high-level DEVICE STATUS; the controller turns that into
//  concrete on/off blink patterns. PWM brightness is a capability flag
//  (Phase 1 A-3: PWM unverified → default OFF), gated in the port.
// =====================================================================
#pragma once

#include <cstdint>
#include "touch_types.h"   // Millis

namespace ss {

// High-level device status the rest of the system sets; the controller
// maps each to a status-LED pattern. Ordered loosely by attention.
enum class DeviceStatus : uint8_t {
    Booting        = 0,  // brief, just powered
    Provisioning   = 1,  // SoftAP portal active (Phase 7)
    Connecting     = 2,  // joining Wi-Fi / broker
    Online         = 3,  // normal operation, all good
    Offline        = 4,  // running locally, no cloud (still fully functional)
    Fault          = 5,  // a fault condition (Phase 9 fault class)
    FactoryReset   = 6,  // reset gesture in progress (staged feedback, Q4)
};

// Backlight is a simple ON/OFF by default (Phase 1 A-3 confirmed);
// brightness 0..100 is honored ONLY if the profile declares PWM capable.
struct BacklightSetting {
    bool    on         = false;
    uint8_t brightness = 100;   // ignored unless PWM-capable
};

}  // namespace ss
