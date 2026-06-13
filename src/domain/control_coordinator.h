// =====================================================================
//  domain/control_coordinator.h — ControlCoordinator (Core Domain)
// ---------------------------------------------------------------------
//  The center of the control plane. It is the ONE place that turns
//  inputs into relay actions:
//    • implements TouchSink         — physical touch → relay (local)
//    • implements ControlCommandPort — cloud/LAN/boot commands → relay
//    • drives RelayController        — the only relay owner
//    • announces via StateChangePort — telemetry/indicator (best-effort)
//    • forwards factory-reset gesture to an injected handler
//
//  Prime invariant (Phase 1): local touch ALWAYS acts, synchronously,
//  regardless of network/cloud state. There is no code path here where a
//  connectivity condition can block a local press — connectivity only
//  ever *observes* (StateChangePort) or *requests* (ControlCommandPort).
//
//  Owned by the Control task (Phase 3) → no internal locking. Pure logic
//  over ports → fully host-testable. Clean Code: small functions, CQS.
// =====================================================================
#pragma once

#include "relay_controller.h"
#include "touch_types.h"
#include "config.h"
#include "../ports/touch_sink.h"
#include "../ports/control_command_port.h"
#include "../ports/state_change_port.h"

namespace ss {

// Optional handler for the factory-reset gesture (Q4). Kept as a separate
// injected interface so the coordinator doesn't depend on the (heavy,
// connectivity-side) reset machinery — it just signals "user asked".
class FactoryResetHandler {
public:
    virtual ~FactoryResetHandler() = default;
    virtual void onFactoryResetRequested() = 0;
    virtual void onFactoryResetProgress(Millis elapsedMs, Millis totalMs) = 0;
};

class NullFactoryResetHandler : public FactoryResetHandler {
public:
    void onFactoryResetRequested() override {}
    void onFactoryResetProgress(Millis, Millis) override {}
};

// How a single-channel touch maps to relay action. Default: a confirmed
// press toggles the channel (standard wall-switch feel). Long-press is
// reserved (e.g. future scene/dim) and currently announced-only.
class ControlCoordinator final : public TouchSink, public ControlCommandPort {
public:
    ControlCoordinator(RelayController& relays,
                       StateChangePort& stateOut,
                       FactoryResetHandler& reset)
        : relays_(relays), stateOut_(stateOut), reset_(reset) {}

    // ---------- TouchSink (local, physical — highest authority) ----------
    void onTouchEvent(ChannelId ch, TouchEvent ev) override {
        if (ev == TouchEvent::Pressed)         // press toggles (wall-switch feel)
            applyToggle(ch, CommandSource::Local);
        // Released / LongPress: no relay action by default (reserved).
    }

    void onFactoryResetGesture() override { reset_.onFactoryResetRequested(); }
    void onFactoryResetProgress(Millis e, Millis t) override {
        reset_.onFactoryResetProgress(e, t);
    }

    // ---------- ControlCommandPort (cloud / LAN / boot) ----------
    Status submit(const ControlCommand& cmd) override {
        switch (cmd.kind) {
            case CommandKind::SetChannel:    return applySet(cmd.channel, cmd.state, cmd.source);
            case CommandKind::ToggleChannel: return applyToggle(cmd.channel, cmd.source);
            case CommandKind::SetAll:        return applySetAll(cmd.state, cmd.source);
        }
        return Status::fail(Error::InvalidArgument);
    }

private:
    // Each apply* drives the relay then announces ONLY on a real change,
    // so telemetry/indicator never see no-op repeats (Phase 6 dedupe).
    Status applySet(ChannelId ch, RelayState want, CommandSource src) {
        const RelayState before = relays_.state(ch);
        if (Status s = relays_.set(ch, want); !s) return s;
        announceIfChanged(ch, before, src);
        return Status::success();
    }

    Status applyToggle(ChannelId ch, CommandSource src) {
        const RelayState before = relays_.state(ch);
        if (Status s = relays_.toggleChannel(ch); !s) return s;
        announceIfChanged(ch, before, src);
        return Status::success();
    }

    Status applySetAll(RelayState want, CommandSource src) {
        // Capture before-states, apply, then announce per changed channel.
        RelayState before[kMaxChannels];
        const ChannelId n = relays_.channelCount();
        for (ChannelId ch = 0; ch < n; ++ch) before[ch] = relays_.state(ch);
        if (Status s = relays_.setAll(want); !s) return s;
        for (ChannelId ch = 0; ch < n; ++ch) announceIfChanged(ch, before[ch], src);
        return Status::success();
    }

    void announceIfChanged(ChannelId ch, RelayState before, CommandSource src) {
        const RelayState now = relays_.state(ch);
        if (now != before) stateOut_.onStateChanged(ch, now, src);
    }

    RelayController&     relays_;
    StateChangePort&     stateOut_;
    FactoryResetHandler& reset_;
};

}  // namespace ss
