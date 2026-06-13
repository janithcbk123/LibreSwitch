// =====================================================================
//  test/host/test_touch_input.cpp — L1 unit tests (Phase 10)
// ---------------------------------------------------------------------
//  Time-injected tests for TouchInput: debounce, long-press, the Q4
//  two-button factory-reset gesture + staged progress + re-arm, and
//  adversarial edges (glitch rejection, single-button never resets).
// =====================================================================
#include "test_framework.h"
#include "../../src/domain/touch_input.h"

using namespace ss;

struct MockTouchSink : TouchSink {
    struct Ev { ChannelId ch; TouchEvent ev; };
    std::vector<Ev> events;
    int   resetGestures = 0;
    int   progressTicks = 0;
    Millis lastElapsed = 0, lastTotal = 0;

    void onTouchEvent(ChannelId ch, TouchEvent ev) override { events.push_back({ch, ev}); }
    void onFactoryResetGesture() override { ++resetGestures; }
    void onFactoryResetProgress(Millis e, Millis t) override { ++progressTicks; lastElapsed = e; lastTotal = t; }

    int count(ChannelId ch, TouchEvent ev) const {
        int n = 0; for (auto& e : events) if (e.ch == ch && e.ev == ev) ++n; return n;
    }
};

// Feed a single channel a constant level from t=start..start+span (inclusive)
// in `stepMs` increments, returning the final time.
static Millis feed(TouchInput& t, ChannelId ch, TouchLevel lvl,
                   Millis start, Millis span, Millis stepMs = 10) {
    Millis now = start;
    for (Millis e = 0; e <= span; e += stepMs) { now = start + e; t.poll(ch, lvl, now); }
    return now;
}

TEST(poll_before_init_fails) {
    MockTouchSink s; TouchInput t(s, TouchConfig{});
    EQ(t.poll(0, TouchLevel::Pressed, 0).error, Error::NotInitialized);
}

TEST(debounce_emits_pressed_after_threshold) {
    MockTouchSink s; TouchConfig c; c.debounceMs = 30;
    TouchInput t(s, c); t.init(2);
    feed(t, 0, TouchLevel::Pressed, 0, 40);     // held > 30 ms
    CHECK(t.isPressed(0));
    EQ(s.count(0, TouchEvent::Pressed), 1);
}

TEST(glitch_shorter_than_debounce_is_rejected) {
    MockTouchSink s; TouchConfig c; c.debounceMs = 30;
    TouchInput t(s, c); t.init(2);
    // press for only 20 ms then release — should NOT register as pressed
    t.poll(0, TouchLevel::Pressed, 0);
    t.poll(0, TouchLevel::Pressed, 20);
    t.poll(0, TouchLevel::Released, 25);
    feed(t, 0, TouchLevel::Released, 25, 40);
    CHECK(!t.isPressed(0));
    EQ(s.count(0, TouchEvent::Pressed), 0);     // glitch rejected
}

TEST(press_then_release_emits_both) {
    MockTouchSink s; TouchConfig c; c.debounceMs = 30;
    TouchInput t(s, c); t.init(1);
    Millis now = feed(t, 0, TouchLevel::Pressed, 0, 50);
    feed(t, 0, TouchLevel::Released, now + 10, 50);
    EQ(s.count(0, TouchEvent::Pressed), 1);
    EQ(s.count(0, TouchEvent::Released), 1);
    CHECK(!t.isPressed(0));
}

TEST(long_press_fires_once_after_threshold) {
    MockTouchSink s; TouchConfig c; c.debounceMs = 30; c.longPressMs = 800;
    TouchInput t(s, c); t.init(1);
    feed(t, 0, TouchLevel::Pressed, 0, 1000);   // hold > 800 ms
    EQ(s.count(0, TouchEvent::LongPress), 1);   // exactly once, not repeated
    EQ(s.count(0, TouchEvent::Pressed), 1);
}

TEST(two_button_hold_triggers_factory_reset) {
    MockTouchSink s; TouchConfig c;
    c.debounceMs = 30; c.factoryHoldMs = 10000; c.resetChannelA = 0; c.resetChannelB = 1;
    TouchInput t(s, c); t.init(2);
    // released baseline first (> settle window) — boot-safety requirement
    Millis e = 0;
    for (; e < 700; e += 50) { t.poll(0, TouchLevel::Released, e); t.poll(1, TouchLevel::Released, e); }
    // then hold both channels for > 10 s, polling both each step
    const Millis holdStart = e;
    for (; e <= holdStart + 10200; e += 50) {
        t.poll(0, TouchLevel::Pressed, e);
        t.poll(1, TouchLevel::Pressed, e);
    }
    EQ(s.resetGestures, 1);
    CHECK(s.progressTicks > 0);                 // staged feedback emitted
    EQ(s.lastTotal, (Millis)10000);
}

TEST(single_button_never_triggers_factory_reset) {
    MockTouchSink s; TouchConfig c; c.factoryHoldMs = 10000;
    TouchInput t(s, c); t.init(2);
    feed(t, 0, TouchLevel::Pressed, 0, 12000, 50);   // only channel 0 held
    EQ(s.resetGestures, 0);
}

TEST(releasing_pair_before_threshold_cancels_reset) {
    MockTouchSink s; TouchConfig c; c.factoryHoldMs = 10000;
    TouchInput t(s, c); t.init(2);
    for (Millis e = 0; e <= 5000; e += 50) {     // hold both only 5 s
        t.poll(0, TouchLevel::Pressed, e);
        t.poll(1, TouchLevel::Pressed, e);
    }
    for (Millis e = 5050; e <= 7000; e += 50) {  // release both
        t.poll(0, TouchLevel::Released, e);
        t.poll(1, TouchLevel::Released, e);
    }
    EQ(s.resetGestures, 0);                      // never reached 10 s
}

TEST(reset_gesture_rearms_after_release) {
    MockTouchSink s; TouchConfig c; c.factoryHoldMs = 10000;
    TouchInput t(s, c); t.init(2);
    // released baseline (required before any hold can arm — boot-safety, > settle)
    Millis e = 0;
    for (; e < 700; e += 50) { t.poll(0, TouchLevel::Released, e); t.poll(1, TouchLevel::Released, e); }
    // first full gesture
    for (; e <= 700 + 10400; e += 50) { t.poll(0, TouchLevel::Pressed, e); t.poll(1, TouchLevel::Pressed, e); }
    // release (long enough to re-arm)
    Millis relEnd = e + 800;
    for (; e <= relEnd; e += 50) { t.poll(0, TouchLevel::Released, e); t.poll(1, TouchLevel::Released, e); }
    // second full gesture
    Millis start2 = e;
    for (; e <= start2 + 10400; e += 50) { t.poll(0, TouchLevel::Pressed, e); t.poll(1, TouchLevel::Pressed, e); }
    EQ(s.resetGestures, 2);                      // fired again after re-arm
}

TEST(reset_pair_invalid_on_single_gang_is_disabled) {
    MockTouchSink s; TouchConfig c; c.resetChannelA = 0; c.resetChannelB = 1;
    TouchInput t(s, c); t.init(1);               // only ch 0 exists
    feed(t, 0, TouchLevel::Pressed, 0, 12000, 50);
    EQ(s.resetGestures, 0);                       // gesture disabled, no crash
}

TEST(init_rejects_invalid_channel_count) {
    MockTouchSink s; TouchInput t(s, TouchConfig{});
    EQ(t.init(0).error, Error::InvalidArgument);
    EQ(t.init(kMaxChannels + 1).error, Error::InvalidArgument);
}


// on-device finding: a reset pair already pressed at the FIRST scan must
// NOT trigger a factory reset (a stuck/noisy boot read caused a reset
// boot loop on hardware). A real gesture is released → held.
TEST(reset_gesture_ignores_press_asserted_at_boot) {
    MockTouchSink s; TouchConfig c; c.factoryHoldMs = 10000;
    TouchInput t(s, c); t.init(2);
    for (Millis e = 0; e <= 15000; e += 50) {        // both pressed from t=0
        t.poll(0, TouchLevel::Pressed, e);
        t.poll(1, TouchLevel::Pressed, e);
    }
    EQ(s.resetGestures, 0);                          // never fires (no released baseline)
}

TEST(reset_gesture_fires_after_clean_release_then_hold) {
    MockTouchSink s; TouchConfig c; c.factoryHoldMs = 10000;
    TouchInput t(s, c); t.init(2);
    for (Millis e = 0; e < 700; e += 50) {           // released baseline (> settle)
        t.poll(0, TouchLevel::Released, e); t.poll(1, TouchLevel::Released, e);
    }
    for (Millis e = 700; e <= 11200; e += 50) {      // genuine 10 s+ hold
        t.poll(0, TouchLevel::Pressed, e); t.poll(1, TouchLevel::Pressed, e);
    }
    EQ(s.resetGestures, 1);
}

int main() {
    printf("TouchInput unit tests\n");
    return tf::run_all();
}
