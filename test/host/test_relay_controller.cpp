// =====================================================================
//  test/host/test_relay_controller.cpp — L1 unit tests (Phase 10)
// ---------------------------------------------------------------------
//  Verifies RelayController logic on host with a mock RelaySink. Targets
//  the ≥90% core-domain coverage bar (Phase 10 decision #3). Includes
//  adversarial edges (bad channel, uninitialised use) per Phase 10 L5
//  spirit applied at unit level.
// =====================================================================
#include "test_framework.h"
#include "../../src/domain/relay_controller.h"

using namespace ss;

// ---- Mock sink: records every drive call, can simulate HW failure ----
struct MockSink : RelaySink {
    struct Call { ChannelId ch; RelayState st; };
    std::vector<Call> calls;
    bool returnValue = true;

    bool driveRelay(ChannelId ch, RelayState state) override {
        calls.push_back({ch, state});
        return returnValue;
    }
    RelayState last(ChannelId ch) const {
        RelayState s = RelayState::Off;
        for (auto& c : calls) if (c.ch == ch) s = c.st;
        return s;
    }
};

TEST(init_rejects_zero_channels) {
    MockSink sink; RelayController rc(sink);
    EQ(rc.init(0).error, Error::InvalidArgument);
    CHECK(!rc.initialized());
}

TEST(init_rejects_too_many_channels) {
    MockSink sink; RelayController rc(sink);
    EQ(rc.init(kMaxChannels + 1).error, Error::InvalidArgument);
}

TEST(init_drives_all_channels_off_by_default) {
    MockSink sink; RelayController rc(sink);
    EQ(rc.init(4).error, Error::None);
    EQ(rc.channelCount(), (ChannelId)4);
    // cold-boot OFF (Phase 1 A-2): each channel driven once, to Off
    EQ(sink.calls.size(), (size_t)4);
    for (ChannelId ch = 0; ch < 4; ++ch) EQ(rc.state(ch), RelayState::Off);
}

TEST(set_before_init_fails) {
    MockSink sink; RelayController rc(sink);
    EQ(rc.set(0, RelayState::On).error, Error::NotInitialized);
    CHECK(sink.calls.empty());
}

TEST(set_valid_channel_drives_and_records) {
    MockSink sink; RelayController rc(sink);
    rc.init(2);
    sink.calls.clear();
    EQ(rc.set(1, RelayState::On).error, Error::None);
    EQ(rc.state(1), RelayState::On);
    EQ(sink.calls.size(), (size_t)1);
    EQ(sink.calls[0].ch, (ChannelId)1);
    EQ(sink.calls[0].st, RelayState::On);
}

TEST(set_channel_not_present_is_rejected) {
    MockSink sink; RelayController rc(sink);
    rc.init(2);              // only ch 0,1 present
    sink.calls.clear();
    EQ(rc.set(3, RelayState::On).error, Error::ChannelNotPresent);
    CHECK(sink.calls.empty());          // no spurious hardware write
    EQ(rc.state(3), RelayState::Off);   // out-of-range query is safe
}

TEST(toggle_flips_state) {
    MockSink sink; RelayController rc(sink);
    rc.init(1);
    EQ(rc.state(0), RelayState::Off);
    rc.toggleChannel(0);
    EQ(rc.state(0), RelayState::On);
    rc.toggleChannel(0);
    EQ(rc.state(0), RelayState::Off);
}

TEST(set_all_affects_only_present_channels) {
    MockSink sink; RelayController rc(sink);
    rc.init(3);
    sink.calls.clear();
    EQ(rc.setAll(RelayState::On).error, Error::None);
    EQ(sink.calls.size(), (size_t)3);   // exactly the 3 present channels
    for (ChannelId ch = 0; ch < 3; ++ch) EQ(rc.state(ch), RelayState::On);
}

TEST(state_query_has_no_side_effects) {
    MockSink sink; RelayController rc(sink);
    rc.init(2);
    sink.calls.clear();
    (void)rc.state(0);
    (void)rc.channelCount();
    CHECK(sink.calls.empty());          // queries never drive hardware
}

TEST(hardware_failure_does_not_corrupt_logical_state) {
    MockSink sink; RelayController rc(sink);
    rc.init(1);
    sink.returnValue = false;           // simulate HW-layer failure
    EQ(rc.set(0, RelayState::On).error, Error::None);  // domain authoritative
    EQ(rc.state(0), RelayState::On);    // logical state still tracked
}

int main() {
    printf("RelayController unit tests\n");
    return tf::run_all();
}
