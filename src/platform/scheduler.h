// =====================================================================
//  platform/scheduler.h — task/scheduler seam (Platform layer)
// ---------------------------------------------------------------------
//  The seam over the RTOS task primitives. The composition root creates
//  tasks through this interface; the on-device impl uses FreeRTOS (from
//  the Beken SDK — CFG_OS_FREERTOS=1). Keeping creation behind a seam
//  means the COMPOSITION (which tasks, what priorities, what bodies) is
//  expressed in portable code, and only the create/delay calls are
//  device-specific.
//
//  Phase 3 task set (priorities): Control(5), Connectivity(3), Mqtt(2),
//  Ota(2), CloudSync(1), Health(1). This seam carries the priority +
//  stack so the composition root states them explicitly.
//
//  ON-DEVICE VERIFICATION ITEM (R-33): Phase 3 specified STATIC task
//  allocation (xTaskCreateStatic), which requires
//  configSUPPORT_STATIC_ALLOCATION=1 in the Beken SDK's FreeRTOSConfig.
//  That flag could NOT be confirmed from the LibreTiny clone (FreeRTOS
//  comes from the SDK at build time). The impl below is written for
//  static allocation; if the SDK lacks the flag, it falls back to
//  xTaskCreate (dynamic) — to be confirmed at bring-up.
// =====================================================================
#pragma once

#include <cstdint>

namespace ss {

using TaskBody = void (*)(void* arg);

struct TaskSpec {
    const char* name;
    TaskBody    body;
    void*       arg;
    uint16_t    stackWords;   // stack in words (FreeRTOS convention)
    uint8_t     priority;
};

class Scheduler {
public:
    virtual ~Scheduler() = default;

    // Create (and start) a task. Returns false if creation failed.
    virtual bool createTask(const TaskSpec& spec) = 0;

    // Cooperative delay for the calling task (ms).
    virtual void delayMs(uint32_t ms) = 0;

    // Start the scheduler (never returns on-device once tasks run).
    virtual void start() = 0;
};

}  // namespace ss
