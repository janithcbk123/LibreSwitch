// =====================================================================
//  test/host/test_control_coordinator.cpp — L1 unit tests (Phase 10)
// ---------------------------------------------------------------------
//  Verifies the control-plane center: touch → relay (local authority),
//  command routing with source attribution, announce-only-on-change
//  (telemetry dedupe), factory-reset forwarding. Uses a REAL
//  RelayController (integration of two domain pieces) + mock ports.
// =====================================================================
#include "test_framework.h"
#include "../../src/domain/control_coordinator.h"

using namespace ss;

struct MockSink : RelaySink {
    bool driveRelay(ChannelId, RelayState) override { return true; }
};

struct MockStateOut : StateChangePort {
    struct Ch { ChannelId ch; RelayState st; CommandSource src; };
    std::vector<Ch> changes;
    void onStateChanged(ChannelId ch, RelayState st, CommandSource src) override {
        changes.push_back({ch, st, src});
    }
};

struct MockReset : FactoryResetHandler {
    int requested = 0, progress = 0;
    void onFactoryResetRequested() override { ++requested; }
    void onFactoryResetProgress(Millis, Millis) override { ++progress; }
};

// Build a wired-up coordinator with N channels ready to use.
struct Rig {
    MockSink sink; RelayController relays{sink};
    MockStateOut out; MockReset reset;
    ControlCoordinator coord{relays, out, reset};
    explicit Rig(ChannelId n) { relays.init(n); }
};

TEST(local_press_toggles_channel) {
    Rig r(2);
    r.coord.onTouchEvent(0, TouchEvent::Pressed);
    EQ(r.relays.state(0), RelayState::On);
    r.coord.onTouchEvent(0, TouchEvent::Pressed);
    EQ(r.relays.state(0), RelayState::Off);
}

TEST(local_press_announces_change_with_local_source) {
    Rig r(2);
    r.coord.onTouchEvent(1, TouchEvent::Pressed);
    EQ(r.out.changes.size(), (size_t)1);
    EQ(r.out.changes[0].ch, (ChannelId)1);
    EQ(r.out.changes[0].st, RelayState::On);
    EQ(r.out.changes[0].src, CommandSource::Local);
}

TEST(released_and_longpress_do_not_actuate_by_default) {
    Rig r(2);
    r.coord.onTouchEvent(0, TouchEvent::Released);
    r.coord.onTouchEvent(0, TouchEvent::LongPress);
    EQ(r.relays.state(0), RelayState::Off);
    CHECK(r.out.changes.empty());
}

TEST(cloud_set_command_routes_and_attributes_source) {
    Rig r(2);
    Status s = r.coord.submit({CommandKind::SetChannel, 0, RelayState::On, CommandSource::Cloud});
    EQ(s.error, Error::None);
    EQ(r.relays.state(0), RelayState::On);
    EQ(r.out.changes.back().src, CommandSource::Cloud);
}

TEST(toggle_command_routes) {
    Rig r(1);
    r.coord.submit({CommandKind::ToggleChannel, 0, RelayState::Off, CommandSource::Lan});
    EQ(r.relays.state(0), RelayState::On);
    EQ(r.out.changes.back().src, CommandSource::Lan);
}

TEST(set_all_command_announces_each_changed_channel) {
    Rig r(3);
    Status s = r.coord.submit({CommandKind::SetAll, 0, RelayState::On, CommandSource::Cloud});
    EQ(s.error, Error::None);
    EQ(r.out.changes.size(), (size_t)3);          // all three went Off→On
    for (auto& c : r.out.changes) { EQ(c.st, RelayState::On); EQ(c.src, CommandSource::Cloud); }
}

TEST(no_op_set_does_not_announce) {
    Rig r(2);
    // channel already Off; setting Off again must NOT announce (dedupe)
    Status s = r.coord.submit({CommandKind::SetChannel, 0, RelayState::Off, CommandSource::Cloud});
    EQ(s.error, Error::None);
    CHECK(r.out.changes.empty());
}

TEST(set_all_announces_only_channels_that_changed) {
    Rig r(3);
    r.coord.submit({CommandKind::SetChannel, 1, RelayState::On, CommandSource::Local});
    r.out.changes.clear();
    // now setAll(On): ch1 already On (no announce), ch0 & ch2 change
    r.coord.submit({CommandKind::SetAll, 0, RelayState::On, CommandSource::Cloud});
    EQ(r.out.changes.size(), (size_t)2);
    for (auto& c : r.out.changes) CHECK(c.ch != 1);
}

TEST(invalid_channel_command_is_rejected_and_silent) {
    Rig r(2);
    Status s = r.coord.submit({CommandKind::SetChannel, 3, RelayState::On, CommandSource::Cloud});
    EQ(s.error, Error::ChannelNotPresent);
    CHECK(r.out.changes.empty());                 // no announce on failure
}

// The prime invariant: local touch works with NO connectivity wired and
// regardless of any cloud state. NullStateChangePort = "cloud absent".
TEST(local_control_works_with_no_connectivity) {
    MockSink sink; RelayController relays{sink}; relays.init(2);
    NullStateChangePort noOut;                    // connectivity plane absent
    NullFactoryResetHandler noReset;
    ControlCoordinator coord{relays, noOut, noReset};
    coord.onTouchEvent(0, TouchEvent::Pressed);   // must still actuate
    EQ(relays.state(0), RelayState::On);
}

TEST(factory_reset_gesture_is_forwarded) {
    Rig r(2);
    r.coord.onFactoryResetProgress(3000, 10000);
    r.coord.onFactoryResetGesture();
    EQ(r.reset.requested, 1);
    EQ(r.reset.progress, 1);
}

TEST(unknown_command_kind_is_rejected) {
    Rig r(2);
    ControlCommand bad{};
    bad.kind = static_cast<CommandKind>(0xFF);    // not a valid enum value
    EQ(r.coord.submit(bad).error, Error::InvalidArgument);
    CHECK(r.out.changes.empty());
}

int main() {
    printf("ControlCoordinator unit tests\n");
    return tf::run_all();
}
