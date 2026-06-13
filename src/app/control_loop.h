// =====================================================================
//  app/control_loop.h — Control task body (application assembly)
// ---------------------------------------------------------------------
//  Phase 3: the Control task owns the relay writes and the local-input
//  poll loop. This is the BODY of that task — one tick() does the full
//  cycle: scan touch pads, drain queued commands into the coordinator,
//  tick the status indicator. The FreeRTOS task (on-device) just calls
//  tick() forever with a small delay; ALL the logic lives here and is
//  host-testable with the tested domain pieces + a clock seam.
//
//  Why a queue for commands but a direct call for touch: touch is read
//  synchronously in THIS task (lowest latency, the prime invariant —
//  local control never waits on anything). Cloud/LAN commands arrive
//  from OTHER tasks and are handed over via the bounded command queue
//  (Phase 3 reject-when-full: a cloud flood cannot starve local touch).
// =====================================================================
#pragma once

#include "../domain/control_coordinator.h"
#include "../domain/indicator_controller.h"
#include "../adapters/gpio_touch_source.h"
#include "command_sink.h"
#include "../ports/control_command_port.h"

namespace ss {

// Command queue capacity (Phase 3: Command(8, reject-busy)).
inline constexpr size_t kCommandQueueDepth = 8;
using CommandQueue = BoundedQueue<ControlCommand, kCommandQueueDepth>;

class ControlLoop {
public:
    ControlLoop(GpioTouchSource& touch,
                ControlCoordinator& coord,
                IndicatorController& indicator,
                CommandDrain& commands)
        : touch_(touch), coord_(coord), indicator_(indicator), commands_(commands) {}

    // One control cycle at time nowMs. Order matters:
    //  1) drain queued remote commands (bounded work — at most the queue
    //     depth, so a flood can't make one cycle unbounded),
    //  2) scan local touch (synchronous, lowest-latency local control),
    //  3) tick the indicator (LED patterns).
    void tick(Millis nowMs) {
        drainCommands();
        touch_.scan(nowMs);          // local input → coordinator → relays
        indicator_.tick(nowMs);
    }

private:
    // Drain at most the current queue contents (not "until empty" forever,
    // which a fast producer could exploit) — bounded by depth.
    void drainCommands() {
        ControlCommand cmd;
        size_t budget = commands_.capacity();   // hard upper bound per cycle
        while (budget-- > 0 && commands_.pop(cmd))
            coord_.submit(cmd);                  // coordinator owns the action
    }

    GpioTouchSource&     touch_;
    ControlCoordinator&  coord_;
    IndicatorController&  indicator_;
    CommandDrain&       commands_;
};

}  // namespace ss
