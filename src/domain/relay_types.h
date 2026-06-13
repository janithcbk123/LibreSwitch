// =====================================================================
//  domain/relay_types.h — relay value types (Core Domain)
// ---------------------------------------------------------------------
//  Pure value types for relay state. No I/O, no platform. The actual
//  GPIO write is an outbound concern (RelaySink port, implemented by a
//  platform adapter) — the domain never touches hardware directly
//  (Phase 2 dependency rule).
// =====================================================================
#pragma once

#include <cstdint>

namespace ss {

// Channel index is 0-based within the active profile (Phase 1 AO-1:
// 1–4 gang via profile; the domain is channel-count-agnostic).
using ChannelId = uint8_t;

// Relays are non-latching; cold-boot default is OFF (Phase 1 A-2).
enum class RelayState : uint8_t {
    Off = 0,
    On  = 1,
};

constexpr RelayState toggle(RelayState s) {
    return s == RelayState::On ? RelayState::Off : RelayState::On;
}

// Who caused a state change — recorded for telemetry/log (Phase 9 flags,
// Phase 6 evt payload "src"). Kept in the domain because the coordinator
// needs to reason about source (e.g. local always authoritative).
enum class CommandSource : uint8_t {
    Local = 0,   // physical touch (Phase 1 A-1)
    Cloud = 1,   // MQTT command (Phase 6)
    Lan   = 2,   // optional REST (Phase 3, off by default)
    Boot  = 3,   // boot policy restore (Phase 1 A-2)
    Internal = 4 // self-test, etc.
};

}  // namespace ss
