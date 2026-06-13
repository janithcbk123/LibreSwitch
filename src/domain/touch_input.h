// =====================================================================
//  domain/touch_input.h — TouchInput engine (Core Domain)
// ---------------------------------------------------------------------
//  Converts raw, noisy per-channel touch levels into debounced semantic
//  events, detects single-channel long-press, and detects the two-button
//  factory-reset gesture (Q4: a fixed channel pair held together for a
//  hold window, with staged progress for LED feedback).
//
//  Pure + time-injected: the Control task calls poll(ch, level, nowMs)
//  for each channel every cycle; the engine never reads a clock or GPIO
//  itself. This makes every timing path host-testable with synthetic
//  timestamps (Phase 10 L1). Clean Code: small single-purpose helpers.
// =====================================================================
#pragma once

#include "touch_types.h"
#include "result.h"
#include "config.h"
#include "../ports/touch_sink.h"

namespace ss {

// Tunables (provisional; Phase 1 A-1 / Q4). Debounce ~30 ms is typical
// for capacitive touch; long-press 800 ms; factory hold 10 s (Q4).
struct TouchConfig {
    Millis    debounceMs       = 30;
    Millis    longPressMs      = 800;
    Millis    factoryHoldMs    = 10000;   // Q4: 10 s two-button hold
    Millis    progressEveryMs  = 1000;    // emit reset progress each second
    ChannelId resetChannelA    = 0;       // the reset pair (profile-set)
    ChannelId resetChannelB    = 1;
};

class TouchInput {
public:
    TouchInput(TouchSink& sink, const TouchConfig& cfg)
        : sink_(sink), cfg_(cfg) {}

    Status init(ChannelId channelCount) {
        if (channelCount == 0 || channelCount > kMaxChannels)
            return Status::fail(Error::InvalidArgument);
        // The reset pair must exist on this profile; if not (e.g. 1-gang),
        // the gesture is simply disabled (switch_1g uses an alternate
        // gesture per Q4 — handled elsewhere, not forced here).
        count_ = channelCount;
        resetPairValid_ = (cfg_.resetChannelA < count_ &&
                           cfg_.resetChannelB < count_ &&
                           cfg_.resetChannelA != cfg_.resetChannelB);
        for (ChannelId ch = 0; ch < kMaxChannels; ++ch) ch_[ch] = Channel{};
        initialized_ = true;
        return Status::success();
    }

    // Feed one channel's raw level at time nowMs. Call once per channel
    // per poll cycle. Drives debounce + long-press + reset-gesture logic.
    Status poll(ChannelId ch, TouchLevel level, Millis nowMs) {
        if (!initialized_) return Status::fail(Error::NotInitialized);
        if (ch >= count_)  return Status::fail(Error::ChannelNotPresent);
        debounce(ch, level, nowMs);
        updateResetGesture(nowMs);
        return Status::success();
    }

    // Query: debounced state of a channel.
    bool isPressed(ChannelId ch) const {
        return (ch < count_) && ch_[ch].stable == TouchLevel::Pressed;
    }

private:
    struct Channel {
        TouchLevel stable      = TouchLevel::Released; // confirmed level
        TouchLevel candidate   = TouchLevel::Released; // pending level
        Millis     since       = 0;     // when candidate first seen
        bool       longFired   = false; // long-press already emitted?
    };

    // --- Debounce + press/release/long-press for one channel ---
    void debounce(ChannelId ch, TouchLevel level, Millis nowMs) {
        Channel& c = ch_[ch];

        if (level != c.candidate) {        // raw level changed → restart timer
            c.candidate = level;
            c.since = nowMs;
            return;
        }
        if (level == c.stable) {           // candidate matches stable → maybe long-press
            maybeLongPress(ch, nowMs);
            return;
        }
        if (elapsed(nowMs, c.since) >= cfg_.debounceMs)  // candidate held long enough
            commitTransition(ch, level, nowMs);
    }

    void commitTransition(ChannelId ch, TouchLevel level, Millis nowMs) {
        Channel& c = ch_[ch];
        c.stable = level;
        c.longFired = false;
        c.since = nowMs;                   // anchor for long-press timing
        sink_.onTouchEvent(ch, level == TouchLevel::Pressed
                                    ? TouchEvent::Pressed
                                    : TouchEvent::Released);
    }

    void maybeLongPress(ChannelId ch, Millis nowMs) {
        Channel& c = ch_[ch];
        if (c.stable == TouchLevel::Pressed && !c.longFired &&
            elapsed(nowMs, c.since) >= cfg_.longPressMs) {
            c.longFired = true;
            sink_.onTouchEvent(ch, TouchEvent::LongPress);
        }
    }

    // --- Two-button factory-reset gesture (Q4) ---
    void updateResetGesture(Millis nowMs) {
        if (!resetPairValid_) return;

        const bool bothHeld = isStablePressed(cfg_.resetChannelA) &&
                              isStablePressed(cfg_.resetChannelB);

        // SAFETY (on-device finding): a press already asserted at boot must
        // NOT count as a reset hold — a stuck/floating/noisy pin (e.g. touch
        // lines disturbed during SoftAP bring-up) reads "held" from t=0 and
        // caused a factory-reset boot loop. A genuine gesture starts from a
        // released pair some time after boot.
        //
        // Rule: the pair must be observed RELEASED at a moment AFTER a short
        // settle window (so the pre-debounce initial-Released state doesn't
        // count, but a real released period before a human press does). Only
        // then may a subsequent hold arm. A press stuck from boot is "held"
        // throughout the settle window and never establishes the baseline.
        if (!baselineReleased_) {
            if (firstPollMs_ == kNoTime) firstPollMs_ = nowMs;
            const bool settled = elapsed(nowMs, firstPollMs_) >= kResetArmSettleMs;
            if (settled && !bothHeld) baselineReleased_ = true;   // clean baseline
            resetActive_ = false;
            resetFired_  = false;
            return;
        }

        if (!bothHeld) {                   // pair released → re-arm for next attempt
            resetActive_ = false;
            resetFired_  = false;
            return;
        }

        if (!resetActive_) {               // pair just became held → start window
            resetActive_ = true;
            resetStart_ = nowMs;
            lastProgress_ = nowMs;
            return;
        }

        const Millis held = elapsed(nowMs, resetStart_);
        emitProgressIfDue(nowMs, held);

        if (held >= cfg_.factoryHoldMs && !resetFired_) {
            resetFired_ = true;
            sink_.onFactoryResetGesture();
        }
    }

    void emitProgressIfDue(Millis nowMs, Millis held) {
        if (resetFired_) return;
        if (elapsed(nowMs, lastProgress_) >= cfg_.progressEveryMs) {
            lastProgress_ = nowMs;
            sink_.onFactoryResetProgress(held, cfg_.factoryHoldMs);
        }
    }

    bool isStablePressed(ChannelId ch) const {
        return ch_[ch].stable == TouchLevel::Pressed;
    }

    // Overflow-safe elapsed time (Millis wraps ~49 days; unsigned
    // subtraction is correct across one wrap).
    static Millis elapsed(Millis now, Millis since) { return now - since; }

    TouchSink&  sink_;
    TouchConfig cfg_;
    Channel     ch_[kMaxChannels] = {};
    ChannelId   count_ = 0;
    bool        initialized_ = false;

    // reset-gesture state
    bool   resetPairValid_ = false;
    bool   resetActive_    = false;  // pair currently held
    bool   resetFired_     = false;  // gesture already emitted this hold
    bool   baselineReleased_ = false; // a clean released baseline seen post-settle
    Millis firstPollMs_    = kNoTime; // first reset-gesture evaluation time
    static constexpr Millis kNoTime = 0xFFFFFFFFu;
    static constexpr Millis kResetArmSettleMs = 500;  // ignore boot-stuck reads
    Millis resetStart_     = 0;
    Millis lastProgress_   = 0;
};

}  // namespace ss
