// =====================================================================
//  domain/relay_controller.h — RelayController (Core Domain)
// ---------------------------------------------------------------------
//  Owns the logical relay state for all channels of the active profile
//  and is the ONLY thing that drives the RelaySink. In the running
//  system the Control task owns this instance (Phase 3), so it needs no
//  internal locking. Pure logic + one outbound port → host-testable.
//
//  Clean Code: small single-purpose functions, command/query separation
//  (set* returns Status; state() is a const query), structured errors.
// =====================================================================
#pragma once

#include "relay_types.h"
#include "result.h"
#include "config.h"
#include "../ports/relay_sink.h"

namespace ss {

class RelayController {
public:
    explicit RelayController(RelaySink& sink) : sink_(sink) {}

    // Configure how many channels this profile actually has and drive
    // them all to a known cold-boot state (OFF by default, Phase 1 A-2).
    Status init(ChannelId channelCount, RelayState coldBoot = RelayState::Off) {
        if (channelCount == 0 || channelCount > kMaxChannels)
            return Status::fail(Error::InvalidArgument);
        count_ = channelCount;
        for (ChannelId ch = 0; ch < count_; ++ch)
            applyState(ch, coldBoot);
        initialized_ = true;
        return Status::success();
    }

    // Command: set one channel. Validates, then drives + records.
    Status set(ChannelId ch, RelayState state) {
        if (Status s = guardChannel(ch); !s) return s;
        applyState(ch, state);
        return Status::success();
    }

    // Command: toggle one channel.
    Status toggleChannel(ChannelId ch) {
        if (Status s = guardChannel(ch); !s) return s;
        applyState(ch, toggle(state_[ch]));
        return Status::success();
    }

    // Command: set every present channel at once (e.g. "all off").
    Status setAll(RelayState state) {
        if (!initialized_) return Status::fail(Error::NotInitialized);
        for (ChannelId ch = 0; ch < count_; ++ch)
            applyState(ch, state);
        return Status::success();
    }

    // Query: current logical state of a channel (no side effects).
    RelayState state(ChannelId ch) const {
        return (ch < count_) ? state_[ch] : RelayState::Off;
    }

    // Query: how many channels are active on this profile.
    ChannelId channelCount() const { return count_; }
    bool initialized() const { return initialized_; }

private:
    Status guardChannel(ChannelId ch) const {
        if (!initialized_) return Status::fail(Error::NotInitialized);
        if (ch >= count_)  return Status::fail(Error::ChannelNotPresent);
        return Status::success();
    }

    // The single place that mutates state AND drives hardware — keeps
    // logical state and the physical pin in lockstep.
    void applyState(ChannelId ch, RelayState state) {
        state_[ch] = state;
        sink_.driveRelay(ch, state);
    }

    RelaySink& sink_;
    RelayState state_[kMaxChannels] = {RelayState::Off, RelayState::Off,
                                       RelayState::Off, RelayState::Off};
    ChannelId  count_ = 0;
    bool       initialized_ = false;
};

}  // namespace ss
