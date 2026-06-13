// =====================================================================
//  test/host/test_composition.cpp — wiring/assembly test (host)
// ---------------------------------------------------------------------
//  The on-device CompositionRoot itself can't run on host (it uses
//  LibreTiny/FlashDB). But its WIRING LOGIC — construction order,
//  profile-driven bring-up, boot-state application, the full assembled
//  control path — can be verified by composing the SAME pieces with mock
//  platform impls. This catches assembly bugs (bad order, lifetime, a
//  miswired port) without hardware; only the concrete impl instantiation
//  is left for on-device proof.
//
//  This mirrors composition_root.h step-for-step using mocks for the
//  three platform seams (GpioHal, KvStore, Clock).
// =====================================================================
#include "test_framework.h"
#include "../../src/app/control_loop.h"
#include "../../src/app/command_sink.h"
#include "../../src/app/fan_out_state_change.h"
#include "../../src/platform/sync_queue.h"
#include "../../src/platform/critical_section.h"
#include "../../src/app/logging_state_listener.h"
#include "../../src/app/persisting_state_listener.h"
#include "../../src/log/logger.h"
#include "../../src/adapters/gpio_relay_sink.h"
#include "../../src/adapters/gpio_touch_source.h"
#include "../../src/adapters/gpio_indicator_sink.h"
#include "../../src/adapters/flashdb_state_persistence.h"
#include "../../src/domain/boot_policy.h"
#include "../../src/profiles/profile_resolver.h"
#include "../../src/platform/clock.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <optional>

using namespace ss;

struct MockGpio : GpioHal {
    std::map<uint8_t,bool> level;
    void configureOutput(uint8_t) override {}
    void writePin(uint8_t pin, bool high) override { level[pin] = high; }
    void configureInput(uint8_t, bool) override {}
    bool readPin(uint8_t pin) override { return level.count(pin) ? level[pin] : false; }
};
struct MockKv : KvStore {
    std::map<std::string, std::vector<uint8_t>> store;
    bool erase(const char* k) override { store.erase(k); return true; }

    bool setBlob(const char* k, const void* d, size_t n) override {
        const uint8_t* p = static_cast<const uint8_t*>(d);
        store[k] = std::vector<uint8_t>(p, p+n); return true;
    }
    size_t getBlob(const char* k, void* buf, size_t bufLen) override {
        auto it = store.find(k); if (it == store.end()) return 0;
        size_t n = std::min(bufLen, it->second.size());
        std::memcpy(buf, it->second.data(), n); return it->second.size();
    }
};
struct MockClock : Clock { Millis t = 0; Millis nowMs() override { return t; } };
struct MockSinkTsdb : TsdbSink {
    std::vector<LogRecord> records;
    bool append(const uint8_t* rec, size_t len, uint32_t) override {
        LogRecord r{}; if (!LogCodec::decode(rec, len, r)) return false;
        records.push_back(r); return true;
    }
};
struct MockTimeSrc : TimeSource {
    uint32_t uptimeMs() override { return 1000; }
    uint32_t utcEpoch() override { return 0; }
    bool ntpSynced() override { return false; }
};
struct MockIndSink : IndicatorSink {
    bool led = false;
    void setStatusLed(bool on) override { led = on; }
    void setBacklight(bool, uint8_t) override {}
    bool pwmCapable() const override { return false; }
};

// Mirrors CompositionRoot's wiring sequence with mocks.
struct HostComposition {
    MockGpio gpio; MockKv kv; MockClock clock; MockIndSink indSink;
    const DeviceProfile* profile = nullptr;
    TouchConfig tcfg;
    NullStateChangePort stateOut;
    CommandQueue commands{OverflowPolicy::RejectWhenFull};

    std::optional<GpioRelaySink> relaySink;
    std::optional<RelayController> relays;
    std::optional<IndicatorController> indicator;
    std::optional<NullFactoryResetHandler> reset;
    std::optional<ControlCoordinator> coord;
    std::optional<TouchInput> touchEngine;
    std::optional<GpioTouchSource> touchSrc;
    std::optional<FlashDbStatePersistence> persistence;
    std::optional<BoundedCommandSource<kCommandQueueDepth>> cmdSource;
    std::optional<CommandQueuePort> cmdPort;
    std::optional<ControlLoop> loop;

    bool begin(uint8_t profileByte) {
        kv.setBlob("profile_id", &profileByte, 1);
        uint8_t raw = 0;
        size_t n = kv.getBlob("profile_id", &raw, sizeof(raw));
        ProfileLookup lk = (n == sizeof(raw)) ? ProfileResolver::resolveRaw(raw)
                                              : ProfileLookup{nullptr, Error::NotInitialized};
        if (!lk.ok()) return false;
        profile = lk.profile;

        relaySink.emplace(gpio, *profile);
        relays.emplace(*relaySink);
        indicator.emplace(indSink);
        reset.emplace();
        coord.emplace(*relays, stateOut, *reset);
        touchEngine.emplace(*coord, tcfg);
        touchSrc.emplace(gpio, *profile, *touchEngine);
        persistence.emplace(kv);
        cmdSource.emplace(commands);
        cmdPort.emplace(commands);
        loop.emplace(*touchSrc, *coord, *indicator, *cmdSource);

        if (!relaySink->begin().ok()) return false;
        if (!touchSrc->begin().ok()) return false;
        if (!relays->init(profile->channelCount).ok()) return false;
        if (!touchEngine->init(profile->channelCount).ok()) return false;
        BootPolicy boot(*relays, *persistence);
        boot.applyBootState(RestorePolicy::RestoreLast);
        return true;
    }
    void tick() { loop->tick(clock.nowMs()); }
};

TEST(composition_boots_with_valid_profile) {
    HostComposition c;
    CHECK(c.begin(4));                         // switch_4g
    EQ(c.profile->channelCount, (ChannelId)4);
}

TEST(composition_rejects_unknown_profile) {
    HostComposition c;
    CHECK(!c.begin(99));                        // unknown → fault, no guess
}

TEST(composition_boots_relays_off_by_default) {
    HostComposition c;
    c.begin(4);                                // no persisted state → R-7 safe OFF
    for (ChannelId ch = 0; ch < 4; ++ch) EQ(c.relays->state(ch), RelayState::Off);
}

TEST(composition_assembled_local_touch_drives_relay) {
    HostComposition c;
    c.begin(4);
    c.gpio.level[24] = true;                    // press channel 0 pad
    for (Millis t = 0; t <= 60; t += 10) { c.clock.t = t; c.tick(); }
    EQ(c.relays->state(0), RelayState::On);     // full assembled path works
}

TEST(composition_assembled_remote_command_drives_relay) {
    HostComposition c;
    c.begin(2);
    c.cmdPort->submit({CommandKind::SetChannel, 1, RelayState::On, CommandSource::Cloud});
    c.clock.t = 0; c.tick();
    EQ(c.relays->state(1), RelayState::On);
    EQ(c.gpio.level[8], true);                  // reached the pin (ch1 = GPIO8)
}

TEST(composition_restores_persisted_state_on_boot) {
    HostComposition c;
    // pre-seed a valid persisted state via the same adapter
    {
        MockKv seed = c.kv;
        FlashDbStatePersistence p(c.kv);
        PersistedRelayState st{}; st.channel[0] = RelayState::On; st.channel[2] = RelayState::On;
        p.saveRelayState(st);
    }
    c.begin(4);
    EQ(c.relays->state(0), RelayState::On);
    EQ(c.relays->state(1), RelayState::Off);
    EQ(c.relays->state(2), RelayState::On);
}

// ---- cross-task wiring (mirrors composition_root): fan-out + sync queue ----
struct RecordingStateListener : StateChangePort {
    int count = 0; ChannelId lastCh = 0xFF; RelayState lastState = RelayState::Off;
    void onStateChanged(ChannelId ch, RelayState s, CommandSource) override {
        ++count; lastCh = ch; lastState = s;
    }
};

TEST(fanout_delivers_relay_changes_to_registered_listener) {
    // Wire coordinator → fan-out → (a connectivity-like listener), as the
    // composition root wires coordinator → fan-out → MQTT.
    MockGpio gpio;
    GpioRelaySink relaySink(gpio, kSwitch4G); relaySink.begin();
    RelayController relays(relaySink); relays.init(4);
    FanOutStateChange<4> fanOut;
    RecordingStateListener listener;
    CHECK(fanOut.add(&listener));
    NullFactoryResetHandler reset;
    ControlCoordinator coord(relays, fanOut, reset);

    coord.submit({CommandKind::SetChannel, 2, RelayState::On, CommandSource::Cloud});
    EQ(listener.count, 1);                  // change fanned out
    EQ(listener.lastCh, (ChannelId)2);
    EQ(listener.lastState, RelayState::On);
}

TEST(synced_command_queue_bridges_producer_to_loop) {
    // Producer (MQTT-side) submits via CommandQueuePort into a SyncQueue;
    // the loop drains it via SyncCommandSource — the cross-task path.
    NullCriticalSection cs;
    SyncQueue<ControlCommand, kCommandQueueDepth> q(cs, OverflowPolicy::RejectWhenFull);
    SyncCommandSource<kCommandQueueDepth> drain(q);
    CommandQueuePort port(q);

    MockGpio gpio;
    GpioRelaySink relaySink(gpio, kSwitch4G); relaySink.begin();
    RelayController relays(relaySink); relays.init(4);
    NullStateChangePort out; NullFactoryResetHandler reset;
    ControlCoordinator coord(relays, out, reset);
    MockIndSink indSink; IndicatorController indicator(indSink);
    TouchConfig tcfg; TouchInput eng(coord, tcfg); eng.init(4);
    GpioTouchSource touch(gpio, kSwitch4G, eng); touch.begin();
    ControlLoop loop(touch, coord, indicator, drain);

    // producer offers a command across the (synchronized) queue
    EQ(port.submit({CommandKind::SetChannel, 1, RelayState::On, CommandSource::Cloud}).error, Error::None);
    loop.tick(0);                            // consumer drains + acts
    EQ(relays.state(1), RelayState::On);
    EQ(gpio.level[8], true);                 // reached GPIO8 (ch1)
}

TEST(synced_queue_rejects_when_full) {
    NullCriticalSection cs;
    SyncQueue<ControlCommand, kCommandQueueDepth> q(cs, OverflowPolicy::RejectWhenFull);
    CommandQueuePort port(q);
    int accepted = 0;
    for (int i = 0; i < 20; ++i)
        if (port.submit({CommandKind::ToggleChannel, 0, RelayState::Off, CommandSource::Cloud}).ok())
            ++accepted;
    EQ(accepted, (int)kCommandQueueDepth);   // back-pressure preserved through wrapper
}

// logging listener: a relay change reaching the fan-out is logged as a
// control-category event (mirrors composition root wiring).
TEST(logging_listener_records_relay_changes) {
    MockSinkTsdb tsdb; MockTimeSrc time; MockKv kv;
    Logger logger(tsdb, time, kv); logger.begin();
    LoggingStateListener listener(logger);

    FanOutStateChange<4> fanOut;
    fanOut.add(&listener);
    fanOut.onStateChanged(2, RelayState::On, CommandSource::Local);

    bool sawRelayOn = false;
    for (auto& r : tsdb.records)
        if (r.code == (uint16_t)EventCode::RelayOn && r.arg1 == 2) sawRelayOn = true;
    CHECK(sawRelayOn);
}


// persistence listener: a relay change writes the full snapshot to flash
// (the bug was that NOTHING called saveRelayState, so RestoreLast always
// fell back to all-OFF and state never survived a power cycle).
TEST(persisting_listener_saves_snapshot_on_change) {
    MockGpio gpio;
    GpioRelaySink relaySink(gpio, kSwitch4G); relaySink.begin();
    RelayController relays(relaySink); relays.init(4);
    MockKv kv;
    FlashDbStatePersistence persist(kv);
    PersistingStateListener listener(relays, persist);

    // simulate the coordinator having set ch0 + ch2 on, then announcing
    relays.set(0, RelayState::On);
    relays.set(2, RelayState::On);
    listener.onStateChanged(2, RelayState::On, CommandSource::Local);

    // the snapshot must now be loadable and reflect 1,0,1,0
    PersistedRelayState got = persist.loadRelayState();
    CHECK(got.valid);
    EQ(got.channel[0], RelayState::On);
    EQ(got.channel[1], RelayState::Off);
    EQ(got.channel[2], RelayState::On);
    EQ(got.channel[3], RelayState::Off);
}

int main() {
    printf("Composition wiring tests\n");
    return tf::run_all();
}
