// =====================================================================
//  app/fan_out_state_change.h — multiplex StateChangePort (pure)
// ---------------------------------------------------------------------
//  The coordinator announces a relay change through ONE StateChangePort,
//  but several subsystems care: MQTT (publish telemetry), the logger
//  (Phase 9), maybe LAN. This fans one announcement out to N registered
//  listeners. Pure, no heap (fixed slot array), host-testable.
//
//  Called on the Control task's announce path, so each listener's
//  onStateChanged must be non-blocking (they enqueue + return) — the
//  fan-out itself just iterates. Order = registration order.
// =====================================================================
#pragma once

#include "../ports/state_change_port.h"

namespace ss {

template <uint8_t MaxListeners = 4>
class FanOutStateChange final : public StateChangePort {
public:
    // Register a listener. Returns false if full. Listeners are borrowed
    // (must outlive this object — they live in the composition root).
    bool add(StateChangePort* listener) {
        if (!listener || count_ >= MaxListeners) return false;
        listeners_[count_++] = listener;
        return true;
    }

    void onStateChanged(ChannelId ch, RelayState state, CommandSource src) override {
        for (uint8_t i = 0; i < count_; ++i)
            listeners_[i]->onStateChanged(ch, state, src);
    }

private:
    StateChangePort* listeners_[MaxListeners] = {};
    uint8_t          count_ = 0;
};

}  // namespace ss
