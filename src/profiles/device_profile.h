// =====================================================================
//  profiles/device_profile.h — device profile structure (Platform/Profile)
// ---------------------------------------------------------------------
//  Phase 1 AO-1 / Phase 2 Platform layer: a multi-variant platform (1–4
//  gang) driven by a const profile, NOT scattered device-specific logic.
//  Capability-based, vendor-neutral naming (no "BSEED" — Phase 1 AO-1).
//
//  This struct is the single source of truth a profile contributes:
//  channel count, the GPIO pin map adapters bind to, indicator pins +
//  PWM capability (Phase 1 A-3), and the factory-reset pair (Q4). It is
//  pure data — no logic, no platform headers — so it's host-testable and
//  the same definition compiles for host tests and on-device.
// =====================================================================
#pragma once

#include <cstdint>
#include "../domain/relay_types.h"   // ChannelId
#include "../domain/config.h"        // kMaxChannels

namespace ss {

// Sentinel for "no pin" (e.g. unused channel slot, absent backlight).
inline constexpr uint8_t kNoPin = 0xFF;

// Stable, vendor-neutral profile identifier (Phase 1 AO-1). The OTA
// HW-compat group (Phase 3 R-8 / Phase 8) is derived from family +
// channel count, not from a vendor name.
enum class ProfileId : uint8_t {
    Switch1G = 1,
    Switch2G = 2,
    Switch3G = 3,
    Switch4G = 4,
};

// Per-channel hardware mapping. relayPin drives the relay; touchPin reads
// the capacitive pad. Indices 0..(channelCount-1) are valid.
struct ChannelMap {
    uint8_t relayPin = kNoPin;
    uint8_t touchPin = kNoPin;
};

// The complete, const description of one hardware variant.
struct DeviceProfile {
    ProfileId  id;
    const char* name;                 // e.g. "switch_4g" (vendor-neutral)
    ChannelId  channelCount;
    ChannelMap channels[kMaxChannels];

    uint8_t statusLedPin;             // GPIO22 on the reference HW (A-3)
    uint8_t backlightPin;             // GPIO23 (kNoPin if absent)
    bool    pwmCapable;               // A-3: PWM unverified → default false

    // Factory-reset gesture (Q4). For multi-gang, the held pair; for
    // switch_1g, resetPairValid=false signals "use alternate gesture".
    bool      resetPairValid;
    ChannelId resetChannelA;
    ChannelId resetChannelB;

    // OTA HW-compat tag (Phase 3 R-8 / Phase 8): family + channel count.
    // family is shared across the gang variants of the same silicon.
    const char* hwCompatFamily;       // e.g. "bk7231n.relay_touch"
};

}  // namespace ss
