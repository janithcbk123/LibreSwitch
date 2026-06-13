// =====================================================================
//  ports/state_change_port.h — outbound port (Core Domain → adapters)
// ---------------------------------------------------------------------
//  When a relay's state actually changes, the coordinator announces it
//  through this port. Connectivity adapters publish it (Phase 6
//  evt/relay/<ch>/state, with "src"); the indicator/logger observe it.
//  The domain does not know or care who listens — it just announces.
//
//  CRITICAL: implementations MUST be non-blocking and best-effort. They
//  run on the Control task's call path; a slow or failing publisher must
//  NEVER stall local control (Phase 1/2). Adapters enqueue (bounded,
//  drop-oldest telemetry, Phase 3) and return immediately.
// =====================================================================
#pragma once

#include "../domain/relay_types.h"

namespace ss {

class StateChangePort {
public:
    virtual ~StateChangePort() = default;

    // Announce that `ch` is now `state`, changed by `source`. Called only
    // on an actual change (coordinator suppresses no-op repeats), so
    // listeners can treat each call as a real transition.
    virtual void onStateChanged(ChannelId ch, RelayState state,
                                CommandSource source) = 0;
};

// Null implementation for when nothing is wired yet (e.g. early boot,
// or host tests that don't care). Keeps the coordinator usable without
// a connectivity plane present — reinforces offline-first.
class NullStateChangePort : public StateChangePort {
public:
    void onStateChanged(ChannelId, RelayState, CommandSource) override {}
};

}  // namespace ss
