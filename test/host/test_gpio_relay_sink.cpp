// =====================================================================
//  test/host/test_gpio_relay_sink.cpp — L1 unit/integration tests
// ---------------------------------------------------------------------
//  Verifies the GPIO relay adapter logic with a mock GpioHal: pin
//  mapping via profile, active-high/active-low polarity (the inversion
//  bug class), init-to-safe-OFF, bad-channel handling, and a full
//  RelayController → GpioRelaySink → HAL integration proving the
//  domain-to-pin path. The LibreTiny HAL impl is NOT tested here (it is
//  on-device-only, by design).
// =====================================================================
#include "test_framework.h"
#include "../../src/adapters/gpio_relay_sink.h"
#include "../../src/profiles/profile_registry.h"
#include "../../src/domain/relay_controller.h"

using namespace ss;

// Mock HAL records configured pins and the last level written to each.
struct MockGpio : GpioHal {
    std::vector<uint8_t> configured;
    std::vector<std::pair<uint8_t,bool>> writes;
    void configureOutput(uint8_t pin) override { configured.push_back(pin); }
    void writePin(uint8_t pin, bool high) override { writes.push_back({pin, high}); }
    void configureInput(uint8_t, bool) override {}
    bool readPin(uint8_t) override { return false; }
    bool lastLevel(uint8_t pin, bool& out) const {
        bool found = false;
        for (auto& w : writes) if (w.first == pin) { out = w.second; found = true; }
        return found;
    }
};

TEST(begin_configures_all_present_relay_pins_as_output) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch4G);
    EQ(sink.begin().error, Error::None);
    EQ(hal.configured.size(), (size_t)4);
    // reference relay pins 6,8,9,26
    EQ(hal.configured[0], (uint8_t)6);
    EQ(hal.configured[1], (uint8_t)8);
    EQ(hal.configured[2], (uint8_t)9);
    EQ(hal.configured[3], (uint8_t)26);
}

TEST(begin_drives_all_relays_off_active_high) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch4G, RelayPolarity::ActiveHigh);
    sink.begin();
    bool lvl;
    for (uint8_t pin : {6, 8, 9, 26}) {
        CHECK(hal.lastLevel(pin, lvl));
        EQ(lvl, false);             // OFF == LOW for active-high
    }
}

TEST(begin_drives_all_relays_off_active_low) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch4G, RelayPolarity::ActiveLow);
    sink.begin();
    bool lvl;
    for (uint8_t pin : {6, 8, 9, 26}) {
        CHECK(hal.lastLevel(pin, lvl));
        EQ(lvl, true);              // OFF == HIGH for active-low (inverted)
    }
}

TEST(active_high_on_drives_pin_high) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch2G, RelayPolarity::ActiveHigh);
    sink.begin();
    sink.driveRelay(0, RelayState::On);
    bool lvl; hal.lastLevel(6, lvl);
    EQ(lvl, true);                  // ON == HIGH
}

TEST(active_low_on_drives_pin_low) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch2G, RelayPolarity::ActiveLow);
    sink.begin();
    sink.driveRelay(0, RelayState::On);
    bool lvl; hal.lastLevel(6, lvl);
    EQ(lvl, false);                 // ON == LOW (inverted board)
}

TEST(channel_maps_to_correct_pin) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch4G);
    sink.begin();
    hal.writes.clear();
    sink.driveRelay(2, RelayState::On);   // channel 2 → GPIO9
    EQ(hal.writes.size(), (size_t)1);
    EQ(hal.writes[0].first, (uint8_t)9);
}

TEST(out_of_range_channel_returns_false_no_write) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch2G);
    sink.begin();
    hal.writes.clear();
    CHECK(!sink.driveRelay(3, RelayState::On));   // 2-gang has no ch3
    CHECK(hal.writes.empty());
}

// Full path: domain RelayController driving real GpioRelaySink → HAL.
TEST(integration_relaycontroller_drives_gpio) {
    MockGpio hal; GpioRelaySink sink(hal, kSwitch4G, RelayPolarity::ActiveHigh);
    sink.begin();
    RelayController rc(sink);
    rc.init(4);                      // init drives all OFF through the sink
    hal.writes.clear();

    rc.set(1, RelayState::On);       // ch1 → GPIO8 HIGH
    bool lvl; CHECK(hal.lastLevel(8, lvl)); EQ(lvl, true);

    rc.set(1, RelayState::Off);      // ch1 → GPIO8 LOW
    hal.lastLevel(8, lvl); EQ(lvl, false);
}

int main() {
    printf("GpioRelaySink unit/integration tests\n");
    return tf::run_all();
}
