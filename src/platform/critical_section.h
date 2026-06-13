// =====================================================================
//  platform/critical_section.h — critical-section seam (+ impls)
// ---------------------------------------------------------------------
//  The cross-task queues (Mqtt↔Control hand-offs) are touched by two
//  tasks. The pure BoundedQueue is single-threaded-correct only; using
//  it across tasks is a data race on the shared count. This seam guards
//  those accesses. On single-core FreeRTOS a brief critical section is
//  the simple, correct primitive (cheaper than a mutex for tiny ops).
//
//  Host impl: no-op (host tests are single-threaded — no concurrency to
//  guard). Device impl: FreeRTOS taskENTER/EXIT_CRITICAL.
//
//  HONEST NOTE (R-34): this guards the queue's internal consistency. It
//  is NOT a substitute for verifying on-device that the chosen critical
//  section is valid in the contexts used (task vs ISR). We only enqueue
//  from tasks (Mqtt/Control), never ISRs, so task-level critical sections
//  are correct — to confirm at bring-up.
// =====================================================================
#pragma once

namespace ss {

class CriticalSection {
public:
    virtual ~CriticalSection() = default;
    virtual void enter() = 0;
    virtual void exit() = 0;
};

// No-op for host tests (single-threaded).
class NullCriticalSection final : public CriticalSection {
public:
    void enter() override {}
    void exit() override {}
};

// RAII guard.
class CriticalGuard {
public:
    explicit CriticalGuard(CriticalSection& cs) : cs_(cs) { cs_.enter(); }
    ~CriticalGuard() { cs_.exit(); }
    CriticalGuard(const CriticalGuard&) = delete;
    CriticalGuard& operator=(const CriticalGuard&) = delete;
private:
    CriticalSection& cs_;
};

}  // namespace ss

#if defined(LT_BUILD) || defined(ARDUINO)
#include <FreeRTOS.h>
#include <task.h>
namespace ss {
// NOTE: on this Beken FreeRTOS port the taskENTER_CRITICAL()/
// taskEXIT_CRITICAL() macros are NOT statement-safe inside a single-line
// inline method (they expand in a way that breaks parsing). The portable
// underlying functions vPortEnterCritical()/vPortExitCritical() are the
// reliable primitive on single-core ports. (R-34 on-device note.)
extern "C" {
    void vPortEnterCritical(void);
    void vPortExitCritical(void);
}
class FreeRtosCriticalSection final : public CriticalSection {
public:
    void enter() override { vPortEnterCritical(); }
    void exit() override  { vPortExitCritical(); }
};
}  // namespace ss
#endif
