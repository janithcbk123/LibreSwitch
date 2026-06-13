// =====================================================================
//  ports/state_persistence_port.h — outbound port (Core Domain → KVS)
// ---------------------------------------------------------------------
//  Loads/saves the last-known relay state. Implemented by a FlashDB KVS
//  adapter (Phase 4 KVS @ 0x1D8000; Phase 5 config store). The domain
//  uses it ONLY for the RestoreLast policy and to persist state after
//  changes. Pure interface; the domain never touches flash directly.
//
//  Best-effort: a load failure (absent/corrupt) yields valid=false and
//  the BootPolicy falls back to the safe default — persistence never
//  blocks or breaks boot (offline-first).
// =====================================================================
#pragma once

#include "../domain/boot_policy_types.h"
#include "../domain/result.h"

namespace ss {

class StatePersistencePort {
public:
    virtual ~StatePersistencePort() = default;

    // Load last-known state. Implementations return a struct with
    // valid=false on any absence/corruption (never throw, never block).
    virtual PersistedRelayState loadRelayState() = 0;

    // Persist current state (called after changes, debounced/rate-limited
    // by the adapter to spare flash wear — Phase 9 wear concerns).
    virtual Status saveRelayState(const PersistedRelayState& s) = 0;
};

// Null implementation: nothing persisted, loads always invalid. Lets the
// domain run (safe-default boot) with no KVS wired (early bring-up/tests).
class NullStatePersistencePort : public StatePersistencePort {
public:
    PersistedRelayState loadRelayState() override { return PersistedRelayState{}; }
    Status saveRelayState(const PersistedRelayState&) override { return Status::success(); }
};

}  // namespace ss
