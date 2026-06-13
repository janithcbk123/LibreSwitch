// =====================================================================
//  test/host/test_gpio_touch_source.cpp — L1 unit/integration tests
// ---------------------------------------------------------------------
//  Touch adapter logic with a scriptable mock GpioHal: pin mapping,
//  active-high/low conversion, input-pull configuration, and a full
//  pin → adapter → TouchInput → debounced-event integration.
// =====================================================================
#include "test_framework.h"
#include "../../src/adapters/gpio_touch_source.h"
#include "../../src/profiles/profile_registry.h"
#include <map>

using namespace ss;

// Mock HAL: per-pin scripted level, records pull config.
struct MockGpio : GpioHal {
    std::map<uint8_t,bool> level;                 // current read level per pin
    std::map<uint8_t,bool> pullUpCfg;             // how each input was configured
    void configureOutput(uint8_t) override {}
    void writePin(uint8_t, bool) override {}
    void configureInput(uint8_t pin, bool pullUp) override { pullUpCfg[pin] = pullUp; }
    bool readPin(uint8_t pin) override { return level.count(pin) ? level[pin] : false; }
};

// Mock TouchSink to observe what the engine emits at the end of the path.
struct MockTouchSink : TouchSink {
    int pressed = 0, released = 0;
    void onTouchEvent(ChannelId, TouchEvent ev) override {
        if (ev == TouchEvent::Pressed) ++pressed;
        if (ev == TouchEvent::Released) ++released;
    }
    void onFactoryResetGesture() override {}
    void onFactoryResetProgress(Millis, Millis) override {}
};

TEST(begin_configures_present_touch_pins_as_input) {
    MockGpio hal; MockTouchSink s; TouchInput eng(s, TouchConfig{}); eng.init(4);
    GpioTouchSource src(hal, kSwitch4G, eng);
    EQ(src.begin().error, Error::None);
    // reference touch pins 24,20,7,14 all configured
    for (uint8_t pin : {24, 20, 7, 14}) CHECK(hal.pullUpCfg.count(pin) == 1);
}

TEST(active_high_uses_pulldown_idle) {
    MockGpio hal; MockTouchSink s; TouchInput eng(s, TouchConfig{}); eng.init(2);
    GpioTouchSource src(hal, kSwitch2G, eng, TouchActiveLevel::ActiveHigh);
    src.begin();
    EQ(hal.pullUpCfg[24], false);     // active-high → not pull-up (idle low)
}

TEST(active_low_uses_pullup_idle) {
    MockGpio hal; MockTouchSink s; TouchInput eng(s, TouchConfig{}); eng.init(2);
    GpioTouchSource src(hal, kSwitch2G, eng, TouchActiveLevel::ActiveLow);
    src.begin();
    EQ(hal.pullUpCfg[24], true);      // active-low → pull-up (idle high)
}

// Full path: scripted pin HIGH held past debounce → engine emits Pressed.
TEST(integration_active_high_press_produces_pressed_event) {
    MockGpio hal; MockTouchSink s;
    TouchConfig cfg; cfg.debounceMs = 30;
    TouchInput eng(s, cfg); eng.init(2);
    GpioTouchSource src(hal, kSwitch2G, eng, TouchActiveLevel::ActiveHigh);
    src.begin();

    hal.level[24] = true;                          // channel 0 pad pressed (HIGH)
    for (Millis t = 0; t <= 60; t += 10) src.scan(t);   // hold past 30ms debounce
    EQ(s.pressed, 1);
    CHECK(eng.isPressed(0));
}

TEST(integration_active_low_inverts_correctly) {
    MockGpio hal; MockTouchSink s;
    TouchConfig cfg; cfg.debounceMs = 30;
    TouchInput eng(s, cfg); eng.init(2);
    GpioTouchSource src(hal, kSwitch2G, eng, TouchActiveLevel::ActiveLow);
    src.begin();

    hal.level[24] = false;                         // channel 0: LOW == pressed
    hal.level[20] = true;                          // channel 1: HIGH == idle (active-low)
    for (Millis t = 0; t <= 60; t += 10) src.scan(t);
    EQ(s.pressed, 1);                              // only channel 0 pressed
    CHECK(eng.isPressed(0));
    CHECK(!eng.isPressed(1));
}

TEST(scan_maps_each_channel_to_its_pin) {
    MockGpio hal; MockTouchSink s;
    TouchConfig cfg; cfg.debounceMs = 20;
    TouchInput eng(s, cfg); eng.init(4);
    GpioTouchSource src(hal, kSwitch4G, eng, TouchActiveLevel::ActiveHigh);
    src.begin();
    // press only channel 2 (GPIO7); others idle
    hal.level[7] = true;
    for (Millis t = 0; t <= 40; t += 10) src.scan(t);
    CHECK(eng.isPressed(2));
    CHECK(!eng.isPressed(0));
    CHECK(!eng.isPressed(1));
    CHECK(!eng.isPressed(3));
}

TEST(release_produces_released_event) {
    MockGpio hal; MockTouchSink s;
    TouchConfig cfg; cfg.debounceMs = 20;
    TouchInput eng(s, cfg); eng.init(1);
    GpioTouchSource src(hal, kSwitch1G, eng, TouchActiveLevel::ActiveHigh);
    src.begin();
    hal.level[24] = true;
    for (Millis t = 0; t <= 40; t += 10) src.scan(t);    // press
    hal.level[24] = false;
    for (Millis t = 50; t <= 90; t += 10) src.scan(t);   // release
    EQ(s.pressed, 1);
    EQ(s.released, 1);
}

int main() {
    printf("GpioTouchSource unit/integration tests\n");
    return tf::run_all();
}
