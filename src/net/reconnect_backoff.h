// =====================================================================
//  net/reconnect_backoff.h — exponential backoff w/ jitter (pure)
// ---------------------------------------------------------------------
//  Phase 6 D6-4: reconnect with exponential backoff + jitter and a cap,
//  to avoid a fleet thundering-herd hammering the broker after an outage.
//  Pure + time-injected (no clock, no rand state hidden) → host-testable.
//
//  Policy: 1s, 2s, 4s, ... doubling, capped at maxMs. Jitter is a
//  bounded +/- fraction applied via an injected pseudo-random source so
//  tests are deterministic. The Mqtt task asks "should I try now?" each
//  cycle and reports connect success/failure to advance the state.
// =====================================================================
#pragma once

#include <cstdint>
#include "../domain/touch_types.h"   // Millis

namespace ss {

class ReconnectBackoff {
public:
    ReconnectBackoff(Millis baseMs = 1000, Millis maxMs = 60000,
                     uint8_t jitterPct = 20)
        : base_(baseMs), max_(maxMs), jitterPct_(jitterPct), current_(baseMs) {}

    // Call on a failed/lost connection at time now. Schedules the next
    // attempt using the current (doubling) delay + jitter.
    void onFailure(Millis now, uint32_t jitterSeed) {
        const Millis delay = applyJitter(current_, jitterSeed);
        nextAttempt_ = now + delay;
        current_ = nextDouble(current_);
        armed_ = true;
    }

    // Call on a successful connection: reset to the base delay.
    void onSuccess() {
        current_ = base_;
        armed_ = false;
    }

    // May we attempt a connection at time `now`? True if not currently
    // backing off, or the backoff window has elapsed.
    bool ready(Millis now) const {
        return !armed_ || timeReached(now, nextAttempt_);
    }

    Millis currentDelay() const { return current_; }

private:
    Millis nextDouble(Millis d) const {
        const uint64_t doubled = static_cast<uint64_t>(d) * 2;
        return static_cast<Millis>(doubled > max_ ? max_ : doubled);
    }

    // Apply +/- jitterPct_ using the injected seed (deterministic in test).
    Millis applyJitter(Millis d, uint32_t seed) const {
        if (jitterPct_ == 0) return d;
        const Millis span = (d * jitterPct_) / 100;          // max deviation
        if (span == 0) return d;
        const uint32_t r = seed % (2 * span + 1);            // 0..2*span
        return d - span + static_cast<Millis>(r);            // d-span .. d+span
    }

    static bool timeReached(Millis now, Millis target) {
        // overflow-safe (Millis wraps): treat as reached if (now-target)
        // is small-positive.
        return static_cast<int32_t>(now - target) >= 0;
    }

    Millis  base_, max_;
    uint8_t jitterPct_;
    Millis  current_;
    Millis  nextAttempt_ = 0;
    bool    armed_       = false;
};

}  // namespace ss
