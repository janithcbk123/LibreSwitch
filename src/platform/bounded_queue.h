// =====================================================================
//  platform/bounded_queue.h — fixed-capacity command queue (pure logic)
// ---------------------------------------------------------------------
//  Phase 3: bounded queues with explicit overflow policy. This is the
//  PURE ring-buffer logic (capacity, reject-when-full / drop-oldest),
//  host-testable on its own. The actual cross-task hand-off (FreeRTOS
//  queue or a critical-section guard) is a SEPARATE concern layered on
//  top on-device — this type defines the *policy*, not the locking.
//
//  Two policies (Phase 3): RejectWhenFull for commands (back-pressure: a
//  cloud flood must NOT starve local control — excess is rejected, not
//  queued unboundedly) and DropOldest for telemetry (newest matters).
//
//  Single-producer/single-consumer by design (one task enqueues from the
//  command path, the Control task drains). No dynamic allocation.
// =====================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace ss {

enum class OverflowPolicy : uint8_t { RejectWhenFull = 0, DropOldest = 1 };

template <typename T, size_t Capacity>
class BoundedQueue {
    static_assert(Capacity >= 1, "capacity must be >= 1");
public:
    explicit BoundedQueue(OverflowPolicy policy = OverflowPolicy::RejectWhenFull)
        : policy_(policy) {}

    // Enqueue. Returns true if stored. RejectWhenFull → false when full
    // (caller sees back-pressure). DropOldest → evicts the oldest to make
    // room and returns true.
    bool push(const T& item) {
        if (full()) {
            if (policy_ == OverflowPolicy::RejectWhenFull) return false;
            popInternal();                 // DropOldest: discard the oldest
        }
        buf_[tail_] = item;
        tail_ = next(tail_);
        ++count_;
        return true;
    }

    // Dequeue into `out`. Returns false if empty.
    bool pop(T& out) {
        if (empty()) return false;
        out = buf_[head_];
        popInternal();
        return true;
    }

    bool   empty() const { return count_ == 0; }
    bool   full()  const { return count_ == Capacity; }
    size_t size()  const { return count_; }
    size_t capacity() const { return Capacity; }

private:
    void popInternal() { head_ = next(head_); --count_; }
    static size_t next(size_t i) { return (i + 1) % Capacity; }

    T              buf_[Capacity] = {};
    size_t         head_ = 0, tail_ = 0, count_ = 0;
    OverflowPolicy policy_;
};

}  // namespace ss
