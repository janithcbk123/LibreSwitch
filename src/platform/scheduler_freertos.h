// =====================================================================
//  platform/scheduler_freertos.h — FreeRTOS Scheduler impl (ON-DEVICE)
// ---------------------------------------------------------------------
//  Implements the Scheduler seam using the FreeRTOS API the Beken SDK
//  provides (CFG_OS_FREERTOS=1). Compiled on-device only.
//
//  R-33 RESOLUTION (refined): Phase 3 specified STATIC task allocation.
//  Verifying against LibreTiny source, the platform's OWN code (WiFi
//  event task) uses DYNAMIC xTaskCreate — that is the confirmed-available
//  idiom. configSUPPORT_STATIC_ALLOCATION could not be confirmed, so this
//  impl uses xTaskCreate (dynamic). All tasks are created ONCE at boot
//  and never deleted, so dynamic-at-startup has the same steady-state
//  footprint as static (no runtime churn / fragmentation). Switching to
//  xTaskCreateStatic later is a drop-in if the SDK config supports it —
//  to confirm at bring-up. (Honest: using the verified path, not the
//  unverified ideal.)
// =====================================================================
#pragma once

#include "scheduler.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>

namespace ss {

class FreeRtosScheduler final : public Scheduler {
public:
    bool createTask(const TaskSpec& spec) override {
        BaseType_t ok = xTaskCreate(
            spec.body, spec.name, spec.stackWords, spec.arg,
            spec.priority, nullptr);
        return ok == pdPASS;
    }
    void delayMs(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }

    // The platform already started the scheduler before setup() runs, so
    // start() is a no-op here (kept for seam symmetry / host portability).
    void start() override {}
};

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
