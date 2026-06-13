// =====================================================================
//  ports/touch_sink.h — outbound port (TouchInput → coordinator)
// ---------------------------------------------------------------------
//  TouchInput debounces raw levels and emits semantic events through
//  this port. The ControlCoordinator implements it to turn touches into
//  relay commands; tests implement it to assert what the engine emits.
//  Keeps TouchInput pure and host-testable (Phase 10 L1).
// =====================================================================
#pragma once

#include "../domain/touch_types.h"

namespace ss {

class TouchSink {
public:
    virtual ~TouchSink() = default;

    // A debounced per-channel event (Pressed / Released / LongPress).
    virtual void onTouchEvent(ChannelId ch, TouchEvent ev) = 0;

    // The factory-reset gesture (Q4): the configured channel pair held
    // together continuously for the full hold window. Emitted once when
    // the window completes; staged LED feedback is the coordinator's job.
    virtual void onFactoryResetGesture() = 0;

    // Progress ticks for the reset gesture so the indicator can stage
    // feedback (Q4: staged LED). elapsedMs is how long the pair has been
    // held so far; total is the threshold. Emitted while the pair is held.
    virtual void onFactoryResetProgress(Millis elapsedMs, Millis totalMs) = 0;
};

}  // namespace ss
