// =====================================================================
//  test/host/test_control_loop.cpp — L1 unit/integration tests
// ---------------------------------------------------------------------
//  BoundedQueue policies and the ControlLoop cycle: bounded command
//  draining, local touch flowing through the loop to relays, and the
//  prime-invariant property that a full command queue rejects rather
//  than starving local control.
// =====================================================================
#include "test_framework.h"
#include "../../src/app/control_loop.h"
#include "../../src/app/command_sink.h"
#include "../../src/adapters/gpio_relay_sink.h"
#include "../../src/domain/relay_controller.h"
#include "../../src/profiles/profile_registry.h"
#include <map>

using namespace ss;

// ---------- BoundedQueue ----------
TEST(queue_reject_when_full) {
    BoundedQueue<int, 3> q(OverflowPolicy::RejectWhenFull);
    CHECK(q.push(1)); CHECK(q.push(2)); CHECK(q.push(3));
    CHECK(!q.push(4));
    EQ(q.size(), (size_t)3);
    int v; q.pop(v); EQ(v, 1);
}

TEST(queue_drop_oldest) {
    BoundedQueue<int, 3> q(OverflowPolicy::DropOldest);
    q.push(1); q.push(2); q.push(3);
    CHECK(q.push(4));
    int v; q.pop(v); EQ(v, 2);
}

TEST(queue_empty_pop_returns_false) {
    BoundedQueue<int, 2> q;
    int v; CHECK(!q.pop(v));
}

TEST(queue_wraps_correctly) {
    BoundedQueue<int, 2> q;
    q.push(1); q.push(2);
    int v; q.pop(v); q.push(3);
    q.pop(v); EQ(v, 2);
    q.pop(v); EQ(v, 3);
    CHECK(q.empty());
}

// ---------- ControlLoop fixture ----------
struct MockGpio : GpioHal {
    std::map<uint8_t,bool> level;
    void configureOutput(uint8_t) override {}
    void writePin(uint8_t pin, bool high) override { level[pin] = high; }
    void configureInput(uint8_t, bool) override {}
    bool readPin(uint8_t pin) override { return level.count(pin) ? level[pin] : false; }
};
struct MockIndicatorSink : IndicatorSink {
    void setStatusLed(bool) override {}
    void setBacklight(bool, uint8_t) override {}
    bool pwmCapable() const override { return false; }
};

static TouchConfig makeCfg() { TouchConfig c; c.debounceMs = 30; return c; }

struct LoopRig {
    MockGpio gpio;
    GpioRelaySink relaySink{gpio, kSwitch4G};
    RelayController relays{relaySink};
    NullStateChangePort stateOut;
    NullFactoryResetHandler reset;
    ControlCoordinator coord{relays, stateOut, reset};
    TouchConfig tcfg{makeCfg()};
    TouchInput touchEngine{coord, tcfg};
    GpioTouchSource touch{gpio, kSwitch4G, touchEngine, TouchActiveLevel::ActiveHigh};
    MockIndicatorSink indSink;
    IndicatorController indicator{indSink};
    CommandQueue commands{OverflowPolicy::RejectWhenFull};
    BoundedCommandSource<kCommandQueueDepth> cmdSource{commands};
    CommandQueuePort cmdPort{commands};
    ControlLoop loop{touch, coord, indicator, cmdSource};

    LoopRig() {
        relaySink.begin();
        relays.init(4);
        touch.begin();
        touchEngine.init(4);
    }
};

TEST(loop_processes_queued_command_to_relay) {
    LoopRig r;
    CHECK(r.cmdPort.submit({CommandKind::SetChannel, 1, RelayState::On, CommandSource::Cloud}));
    r.loop.tick(0);
    EQ(r.relays.state(1), RelayState::On);
    EQ(r.gpio.level[8], true);
}

TEST(loop_local_touch_drives_relay) {
    LoopRig r;
    r.gpio.level[24] = true;
    for (Millis t = 0; t <= 60; t += 10) r.loop.tick(t);
    EQ(r.relays.state(0), RelayState::On);
}

TEST(full_command_queue_rejects_exactly_depth) {
    LoopRig r;
    int accepted = 0;
    for (int i = 0; i < 20; ++i)
        if (r.cmdPort.submit({CommandKind::ToggleChannel, 0, RelayState::Off, CommandSource::Cloud}))
            ++accepted;
    EQ(accepted, (int)kCommandQueueDepth);
}

TEST(local_touch_works_even_with_saturated_command_queue) {
    LoopRig r;
    for (int i = 0; i < 20; ++i)
        r.cmdPort.submit({CommandKind::SetAll, 0, RelayState::Off, CommandSource::Cloud});
    // local touch on ch0 must still register through the loop
    r.gpio.level[24] = true;
    for (Millis t = 0; t <= 60; t += 10) r.loop.tick(t);
    // after draining 8 SetAll(Off) then a local toggle, ch0 ends ON
    EQ(r.relays.state(0), RelayState::On);
}

TEST(drain_is_bounded_per_cycle) {
    LoopRig r;
    for (size_t i = 0; i < kCommandQueueDepth; ++i)
        r.cmdPort.submit({CommandKind::SetChannel, 0, RelayState::On, CommandSource::Cloud});
    r.loop.tick(0);
    CHECK(r.commands.empty());
}

int main() {
    printf("ControlLoop + BoundedQueue tests\n");
    return tf::run_all();
}
