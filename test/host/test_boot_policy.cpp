// =====================================================================
//  test/host/test_boot_policy.cpp — L1 unit tests (Phase 10)
// ---------------------------------------------------------------------
//  Verifies cold-boot state decisions for every RestorePolicy, and the
//  critical R-7 safety rule: RestoreLast with invalid/absent persisted
//  state MUST fall back to AlwaysOff (never restore garbage / leave a
//  channel in an unsafe-by-accident state).
// =====================================================================
#include "test_framework.h"
#include "../../src/domain/boot_policy.h"

using namespace ss;

struct MockSink : RelaySink {
    bool driveRelay(ChannelId, RelayState) override { return true; }
};

// Configurable persistence mock.
struct MockPersist : StatePersistencePort {
    PersistedRelayState toLoad;        // what loadRelayState returns
    PersistedRelayState lastSaved;
    bool saveCalled = false;
    PersistedRelayState loadRelayState() override { return toLoad; }
    Status saveRelayState(const PersistedRelayState& s) override {
        lastSaved = s; saveCalled = true; return Status::success();
    }
};

struct Rig {
    MockSink sink; RelayController relays{sink};
    MockPersist persist;
    BootPolicy boot{relays, persist};
    explicit Rig(ChannelId n) { relays.init(n); }
};

TEST(apply_before_relay_init_fails) {
    MockSink sink; RelayController relays{sink};   // not init'd
    MockPersist persist; BootPolicy boot{relays, persist};
    EQ(boot.applyBootState(RestorePolicy::AlwaysOff).error, Error::NotInitialized);
}

TEST(always_off_drives_all_off) {
    Rig r(4);
    EQ(r.boot.applyBootState(RestorePolicy::AlwaysOff).error, Error::None);
    for (ChannelId ch = 0; ch < 4; ++ch) EQ(r.relays.state(ch), RelayState::Off);
}

TEST(always_on_drives_all_on) {
    Rig r(3);
    EQ(r.boot.applyBootState(RestorePolicy::AlwaysOn).error, Error::None);
    for (ChannelId ch = 0; ch < 3; ++ch) EQ(r.relays.state(ch), RelayState::On);
}

TEST(restore_last_applies_valid_saved_state) {
    Rig r(3);
    r.persist.toLoad.valid = true;
    r.persist.toLoad.channel[0] = RelayState::On;
    r.persist.toLoad.channel[1] = RelayState::Off;
    r.persist.toLoad.channel[2] = RelayState::On;
    EQ(r.boot.applyBootState(RestorePolicy::RestoreLast).error, Error::None);
    EQ(r.relays.state(0), RelayState::On);
    EQ(r.relays.state(1), RelayState::Off);
    EQ(r.relays.state(2), RelayState::On);
}

// --- R-7 safety: the cases that must NEVER restore garbage ---

TEST(restore_last_with_invalid_state_falls_back_to_off) {
    Rig r(4);
    r.persist.toLoad.valid = false;     // absent/corrupt persisted state
    // even though the (ignored) channel array might contain On values:
    r.persist.toLoad.channel[0] = RelayState::On;
    r.persist.toLoad.channel[1] = RelayState::On;
    EQ(r.boot.applyBootState(RestorePolicy::RestoreLast).error, Error::None);
    for (ChannelId ch = 0; ch < 4; ++ch)
        EQ(r.relays.state(ch), RelayState::Off);   // SAFE fallback
}

TEST(unknown_policy_falls_back_to_off) {
    Rig r(2);
    // set channels On first, then apply a bogus policy → must force Off
    r.relays.setAll(RelayState::On);
    EQ(r.boot.applyBootState(static_cast<RestorePolicy>(0x7F)).error, Error::None);
    for (ChannelId ch = 0; ch < 2; ++ch) EQ(r.relays.state(ch), RelayState::Off);
}

TEST(restore_last_only_touches_present_channels) {
    Rig r(2);                            // 2-gang profile
    r.persist.toLoad.valid = true;
    r.persist.toLoad.channel[0] = RelayState::On;
    r.persist.toLoad.channel[1] = RelayState::On;
    r.persist.toLoad.channel[2] = RelayState::On;  // would be ch2, not present
    EQ(r.boot.applyBootState(RestorePolicy::RestoreLast).error, Error::None);
    EQ(r.relays.channelCount(), (ChannelId)2);
    EQ(r.relays.state(0), RelayState::On);
    EQ(r.relays.state(1), RelayState::On);
    // ch2 not present: query is safe-Off, no spurious drive
    EQ(r.relays.state(2), RelayState::Off);
}

int main() {
    printf("BootPolicy unit tests\n");
    return tf::run_all();
}
