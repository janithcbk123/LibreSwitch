// =====================================================================
//  test/host/test_gpio_indicator_sink.cpp — L1 unit/integration tests
// ---------------------------------------------------------------------
//  Indicator adapter: status LED + backlight pin mapping, polarity,
//  begin-to-OFF, the honest pwmCapable()==false on reference HW (R-32),
//  and a full IndicatorController → GpioIndicatorSink → HAL integration.
// =====================================================================
#include "test_framework.h"
#include "../../src/adapters/gpio_indicator_sink.h"
#include "../../src/domain/indicator_controller.h"
#include "../../src/profiles/profile_registry.h"
#include <map>

using namespace ss;

struct MockGpio : GpioHal {
    std::vector<uint8_t> configured;
    std::map<uint8_t,bool> level;
    void configureOutput(uint8_t pin) override { configured.push_back(pin); }
    void writePin(uint8_t pin, bool high) override { level[pin] = high; }
    void configureInput(uint8_t, bool) override {}
    bool readPin(uint8_t) override { return false; }
};

TEST(begin_configures_led_and_backlight_off) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G);
    EQ(ind.begin().error, Error::None);
    // status LED 22 and backlight 23 configured as outputs
    EQ(hal.configured.size(), (size_t)2);
    EQ(hal.level[22], false);
    EQ(hal.level[23], false);
}

TEST(status_led_on_off_active_high) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G, IndicatorPolarity::ActiveHigh);
    ind.begin();
    ind.setStatusLed(true);
    EQ(hal.level[22], true);
    ind.setStatusLed(false);
    EQ(hal.level[22], false);
}

TEST(status_led_active_low_inverts) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G, IndicatorPolarity::ActiveLow);
    ind.begin();
    ind.setStatusLed(true);
    EQ(hal.level[22], false);          // on == LOW for active-low
}

TEST(backlight_on_off) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch2G);
    ind.begin();
    ind.setBacklight(true, 100);
    EQ(hal.level[23], true);
    ind.setBacklight(false, 0);
    EQ(hal.level[23], false);
}

TEST(backlight_brightness_ignored_on_reference_hw) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G);
    ind.begin();
    // request 30% — but GPIO23 is not PWM-capable; treated as ON (R-32)
    ind.setBacklight(true, 30);
    EQ(hal.level[23], true);           // simply on; brightness has no effect
}

TEST(pwm_capable_is_false_on_reference_hw) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G);
    CHECK(!ind.pwmCapable());          // R-32: GPIO23 is ADC3, not PWM
}

// Full path: IndicatorController status → adapter → HAL pin level.
TEST(integration_controller_online_drives_led_solid_on) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G);
    ind.begin();
    IndicatorController ic(ind);
    ic.setStatus(DeviceStatus::Online);   // solid on
    for (Millis t = 0; t <= 500; t += 50) ic.tick(t);
    EQ(hal.level[22], true);              // status LED on
}

TEST(integration_controller_backlight_respects_pwm_gating) {
    MockGpio hal; GpioIndicatorSink ind(hal, kSwitch4G);
    ind.begin();
    IndicatorController ic(ind);
    // controller asks for 50% but adapter says not PWM-capable → full on
    ic.setBacklight({true, 50});
    EQ(hal.level[23], true);
}

int main() {
    printf("GpioIndicatorSink unit/integration tests\n");
    return tf::run_all();
}
