// =====================================================================
//  domain/touch_types.h — touch value types (Core Domain)
// ---------------------------------------------------------------------
//  Phase 1 A-1: touch inputs are continuous-state (level), not edge
//  pulses, so a 10 s two-button hold is detectable (Q4 factory reset).
//  These types are pure; the raw read comes from a platform adapter via
//  an inbound feed, never sampled directly by the domain.
// =====================================================================
#pragma once

#include <cstdint>
#include "relay_types.h"   // ChannelId

namespace ss {

// Monotonic milliseconds since boot. Supplied by the platform clock; the
// domain never calls millis() itself (testable with synthetic time).
using Millis = uint32_t;

// Raw, pre-debounce level for one touch channel as read from hardware.
enum class TouchLevel : uint8_t {
    Released = 0,
    Pressed  = 1,
};

// Debounced, semantically-meaningful touch events the engine emits.
enum class TouchEvent : uint8_t {
    None        = 0,
    Pressed     = 1,   // confirmed press (debounced)
    Released    = 2,   // confirmed release (debounced)
    LongPress   = 3,   // single channel held past long-press threshold
};

}  // namespace ss
