// =====================================================================
//  app/persisting_state_listener.h — save relay state on change (pure)
// ---------------------------------------------------------------------
//  A StateChangePort that writes the current relay snapshot to the
//  persistence port whenever a relay changes. Registered on the fan-out
//  alongside MQTT + logging, so RestoreLast (Phase boot policy) has a
//  fresh snapshot to restore after a power cycle.
//
//  BUG FIX: previously nothing called saveRelayState(), so RestoreLast
//  always fell back to all-OFF (state never survived a reboot). This
//  listener closes that gap. It reads the authoritative logical state
//  from the RelayController and persists the whole snapshot on each
//  change (simple + correct; writes are infrequent — human button presses
//  — so KVS wear is a non-issue).
// =====================================================================
#pragma once

#include "../ports/state_change_port.h"
#include "../ports/state_persistence_port.h"
#include "../domain/relay_controller.h"

namespace ss {

class PersistingStateListener final : public StateChangePort {
public:
    PersistingStateListener(RelayController& relays, StatePersistencePort& persist)
        : relays_(relays), persist_(persist) {}

    void onStateChanged(ChannelId /*ch*/, RelayState /*state*/, CommandSource /*src*/) override {
        // Snapshot the authoritative logical state and persist it. We save
        // the whole vector (not just the changed channel) so the stored
        // record is always a complete, consistent picture to restore.
        PersistedRelayState snap{};
        for (ChannelId c = 0; c < kMaxChannels; ++c)
            snap.channel[c] = relays_.state(c);
        snap.valid = true;
        persist_.saveRelayState(snap);             // best-effort (Phase: no block)
    }

private:
    RelayController&      relays_;
    StatePersistencePort& persist_;
};

}  // namespace ss
