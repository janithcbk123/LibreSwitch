// =====================================================================
//  app/command_sink.h — command hand-off abstraction
// ---------------------------------------------------------------------
//  ControlLoop drains commands and the connectivity side offers them.
//  To keep ControlLoop independent of WHICH queue (plain single-thread
//  BoundedQueue in host tests; cross-task SyncQueue on device), both the
//  producer and consumer talk to these tiny interfaces. Thin adapters
//  wrap the concrete queues. Pure, host-testable.
// =====================================================================
#pragma once

#include "../ports/control_command_port.h"
#include "../platform/bounded_queue.h"
#include "../platform/sync_queue.h"

namespace ss {

// Consumer side: ControlLoop pops from this.
class CommandDrain {
public:
    virtual ~CommandDrain() = default;
    virtual bool pop(ControlCommand& out) = 0;
    virtual size_t capacity() const = 0;
};

// Producer side: connectivity offers to this. Also an inbound port impl
// so MqttService (which holds a ControlCommandPort&) can push across to
// the Control task safely — the command is QUEUED, never acted on here,
// preserving "Control task owns relays" across the task boundary.
class CommandQueuePort final : public ControlCommandPort {
public:
    // Adapts any object with push(ControlCommand)->bool.
    template <typename Q>
    explicit CommandQueuePort(Q& q)
        : pushFn_(+[](void* p, const ControlCommand& c) {
              return static_cast<Q*>(p)->push(c); }),
          obj_(&q) {}

    Status submit(const ControlCommand& cmd) override {
        return pushFn_(obj_, cmd) ? Status::success() : Status::fail(Error::Busy);
    }

private:
    bool (*pushFn_)(void*, const ControlCommand&);
    void* obj_;
};

// CommandSource adapter over a plain BoundedQueue (host / single-thread).
template <size_t Cap>
class BoundedCommandSource final : public CommandDrain {
public:
    explicit BoundedCommandSource(BoundedQueue<ControlCommand, Cap>& q) : q_(q) {}
    bool pop(ControlCommand& out) override { return q_.pop(out); }
    size_t capacity() const override { return q_.capacity(); }
private:
    BoundedQueue<ControlCommand, Cap>& q_;
};

// CommandSource adapter over a SyncQueue (device / cross-task).
template <size_t Cap>
class SyncCommandSource final : public CommandDrain {
public:
    explicit SyncCommandSource(SyncQueue<ControlCommand, Cap>& q) : q_(q) {}
    bool pop(ControlCommand& out) override { return q_.pop(out); }
    size_t capacity() const override { return q_.capacity(); }
private:
    SyncQueue<ControlCommand, Cap>& q_;
};

}  // namespace ss
