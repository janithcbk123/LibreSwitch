// =====================================================================
//  test/host/test_indicator_controller.cpp — L1 unit tests (Phase 10)
// ---------------------------------------------------------------------
//  Status→pattern mapping, blink timing (time-injected), staged
//  factory-reset feedback (accelerating blink → solid), backlight PWM
//  capability gating (Phase 1 A-3), and LED transition dedupe.
// =====================================================================
#include "test_framework.h"
#include "../../src/domain/indicator_controller.h"

using namespace ss;

struct MockIndicator : IndicatorSink {
    int  statusToggles = 0;
    bool ledOn = false;
    bool blOn = false; uint8_t blBright = 0; int blCalls = 0;
    bool pwm = false;

    void setStatusLed(bool on) override { ledOn = on; ++statusToggles; }
    void setBacklight(bool on, uint8_t b) override { blOn = on; blBright = b; ++blCalls; }
    bool pwmCapable() const override { return pwm; }
};

// Run ticks across [0..span] every stepMs to exercise blink patterns.
static void run(IndicatorController& ic, Millis span, Millis stepMs = 10) {
    for (Millis t = 0; t <= span; t += stepMs) ic.tick(t);
}

TEST(online_status_is_solid_on) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::Online);
    run(ic, 1000);
    CHECK(m.ledOn);                 // solid on for "all good"
}

TEST(connecting_blinks_quickly) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::Connecting);   // 200 ms period
    run(ic, 1000);
    // over 1000ms at 200ms period there should be multiple toggles
    CHECK(m.statusToggles >= 4);
}

TEST(fault_blinks_faster_than_provisioning) {
    MockIndicator a; IndicatorController ica(a);
    ica.setStatus(DeviceStatus::Fault);        // 100 ms
    run(ica, 1000);

    MockIndicator b; IndicatorController icb(b);
    icb.setStatus(DeviceStatus::Provisioning); // 500 ms
    run(icb, 1000);

    CHECK(a.statusToggles > b.statusToggles);  // fault more urgent
}

TEST(led_only_driven_on_transitions) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::Online);        // solid
    run(ic, 500);
    int after = m.statusToggles;
    run(ic, 500);                               // still solid, same level
    EQ(m.statusToggles, after);                 // no redundant drives
}

TEST(reset_progress_accelerates_blink) {
    // Early progress → slow blink; late progress → fast blink. Compare
    // toggle counts over equal windows.
    MockIndicator early; IndicatorController ie(early);
    ie.setStatus(DeviceStatus::FactoryReset);
    ie.setResetProgress(1000, 10000);          // 10% → slow
    run(ie, 1000);

    MockIndicator late; IndicatorController il(late);
    il.setStatus(DeviceStatus::FactoryReset);
    il.setResetProgress(9000, 10000);          // 90% → fast
    run(il, 1000);

    CHECK(late.statusToggles > early.statusToggles);
}

TEST(reset_complete_goes_solid_on) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::FactoryReset);
    ic.setResetProgress(10000, 10000);         // 100% complete
    run(ic, 500);
    CHECK(m.ledOn);                             // solid on = done
}

TEST(clear_reset_returns_to_status_pattern) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::Online);
    ic.setResetProgress(5000, 10000);
    run(ic, 300);
    ic.clearResetProgress();
    int before = m.statusToggles;
    run(ic, 1000);                              // back to Online = solid
    // after clearing, it should settle solid (few/no toggles vs blinking)
    CHECK(m.statusToggles - before <= 2);
}

TEST(backlight_onoff_without_pwm_ignores_brightness) {
    MockIndicator m; m.pwm = false;
    IndicatorController ic(m);
    ic.setBacklight({true, 30});                // request 30%
    CHECK(m.blOn);
    EQ(m.blBright, (uint8_t)100);               // PWM absent → forced full
}

TEST(backlight_pwm_capable_honors_brightness) {
    MockIndicator m; m.pwm = true;
    IndicatorController ic(m);
    ic.setBacklight({true, 30});
    CHECK(m.blOn);
    EQ(m.blBright, (uint8_t)30);                // PWM present → honored
}

TEST(backlight_off) {
    MockIndicator m; m.pwm = true;
    IndicatorController ic(m);
    ic.setBacklight({false, 50});
    CHECK(!m.blOn);
}

TEST(reset_total_zero_does_not_crash) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::FactoryReset);
    ic.setResetProgress(5000, 0);               // guard against /0
    run(ic, 200);
    CHECK(true);                                 // reached here = no crash
}

TEST(booting_status_is_solid_on) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::Booting);
    run(ic, 300);
    CHECK(m.ledOn);                 // solid on briefly at boot
}

TEST(offline_status_blinks_rarely) {
    MockIndicator m; IndicatorController ic(m);
    ic.setStatus(DeviceStatus::Offline);   // 2000 ms heartbeat
    run(ic, 4000);
    // a couple of toggles over 4s, far fewer than Connecting's rate
    CHECK(m.statusToggles >= 1);
    CHECK(m.statusToggles <= 6);
}

int main() {
    printf("IndicatorController unit tests\n");
    return tf::run_all();
}
