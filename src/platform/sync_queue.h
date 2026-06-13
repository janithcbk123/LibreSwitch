// =====================================================================
//  platform/sync_queue.h — cross-task-safe queue wrapper (pure logic)
// ---------------------------------------------------------------------
//  Wraps a BoundedQueue with a CriticalSection so it is safe for the
//  cross-task hand-offs Phase 3 requires (Mqtt task ↔ Control task). The
//  underlying queue keeps its policy (reject-when-full / drop-oldest);
//  this only adds mutual exclusion around push/pop.
//
//  Host-testable with a NullCriticalSection (single-threaded tests don't
//  need real guarding, but the wrapper logic — delegation, return values
//  — is still exercised).
// =====================================================================
#pragma once

#include "bounded_queue.h"
#include "critical_section.h"

namespace ss {

template <typename T, size_t Capacity>
class SyncQueue {
public:
    SyncQueue(CriticalSection& cs, OverflowPolicy policy)
        : cs_(cs), q_(policy) {}

    bool push(const T& item) {
        CriticalGuard g(cs_);
        return q_.push(item);
    }
    bool pop(T& out) {
        CriticalGuard g(cs_);
        return q_.pop(out);
    }
    bool empty() {
        CriticalGuard g(cs_);
        return q_.empty();
    }
    size_t capacity() const { return q_.capacity(); }   // const, no lock needed

private:
    CriticalSection&            cs_;
    BoundedQueue<T, Capacity>   q_;
};

}  // namespace ss
