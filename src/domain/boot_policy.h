// =====================================================================
//  domain/boot_policy.h — BootPolicy (Core Domain)
// ---------------------------------------------------------------------
//  Decides and applies each channel's relay state at cold boot, per the
//  device's RestorePolicy (Phase 1 A-2, risk R-7). Applies through the
//  RelayController with CommandSource::Boot so the action is attributed
//  correctly in telemetry/logs.
//
//  R-7 SAFETY RULE: if policy == RestoreLast but the persisted state is
//  invalid/absent/corrupt, fall back to the SAFE default (AlwaysOff).
//  A device must NEVER come up in an indeterminate or unsafe-by-accident
//  state because persistence was unavailable. Pure logic over ports →
//  host-testable. Clean Code: one decision function per policy.
// =====================================================================
#pragma once

#include "relay_controller.h"
#include "boot_policy_types.h"
#include "config.h"
#include "../ports/state_persistence_port.h"

namespace ss {

class BootPolicy {
public:
    BootPolicy(RelayController& relays, StatePersistencePort& persist)
        : relays_(relays), persist_(persist) {}

    // Apply the cold-boot state for all present channels. Assumes the
    // RelayController is already init()'d (which itself drives a cold
    // default); this then imposes the configured policy on top.
    Status applyBootState(RestorePolicy policy) {
        if (!relays_.initialized()) return Status::fail(Error::NotInitialized);
        switch (policy) {
            case RestorePolicy::AlwaysOff:   return setAllChannels(RelayState::Off);
            case RestorePolicy::AlwaysOn:    return setAllChannels(RelayState::On);
            case RestorePolicy::RestoreLast: return applyRestoreLast();
        }
        // Unknown policy → safe default (defensive; R-7 spirit).
        return setAllChannels(RelayState::Off);
    }

private:
    Status setAllChannels(RelayState state) {
        return relays_.setAll(state);
    }

    // RestoreLast with the R-7 safety fallback baked in.
    Status applyRestoreLast() {
        const PersistedRelayState saved = persist_.loadRelayState();
        if (!saved.valid)                       // absent/corrupt → SAFE default
            return setAllChannels(RelayState::Off);
        return applyPersisted(saved);
    }

    Status applyPersisted(const PersistedRelayState& saved) {
        const ChannelId n = relays_.channelCount();
        for (ChannelId ch = 0; ch < n; ++ch) {
            if (Status s = relays_.set(ch, saved.channel[ch]); !s) return s;
        }
        return Status::success();
    }

    RelayController&      relays_;
    StatePersistencePort& persist_;
};

}  // namespace ss
