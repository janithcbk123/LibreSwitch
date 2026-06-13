// =====================================================================
//  domain/boot_policy_types.h — boot restore policy types (Core Domain)
// ---------------------------------------------------------------------
//  Phase 1 A-2 + risk R-7 (auto-state-restore safety). On cold boot the
//  device must decide each channel's initial relay state. Restoring the
//  pre-power-loss state is convenient but can be unsafe (an appliance
//  snapping on, unattended, after an outage). The choice is therefore an
//  EXPLICIT, profile-driven policy that defaults to the safe option.
// =====================================================================
#pragma once

#include <cstdint>
#include "relay_types.h"

namespace ss {

// Per-device (profile/config) policy for what relays do on cold boot.
enum class RestorePolicy : uint8_t {
    AlwaysOff   = 0,  // SAFE DEFAULT: every channel boots OFF (Phase 1 A-2)
    AlwaysOn    = 1,  // every channel boots ON (rare; e.g. always-on load)
    RestoreLast = 2,  // restore the last-known state (convenient; R-7 risk)
};

// The last-known relay state persisted before power loss, used only when
// policy == RestoreLast. Read through StatePersistencePort.
struct PersistedRelayState {
    RelayState channel[4] = {RelayState::Off, RelayState::Off,
                             RelayState::Off, RelayState::Off};
    bool valid = false;   // false if never persisted / corrupt / absent
};

}  // namespace ss
