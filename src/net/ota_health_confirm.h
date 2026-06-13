// =====================================================================
//  net/ota_health_confirm.h — post-boot health confirm (R-24, pure)
// ---------------------------------------------------------------------
//  Phase 8 D8-4 / R-24. After an OTA commit + reboot, the NEW firmware
//  must prove itself within a bounded window (180 s, Phase 10) by meeting
//  ALL four criteria; otherwise the watchdog triggers rollBack() on the
//  next boot. This class is the pure decision logic — time-injected, no
//  I/O — so every path (confirm, timeout→rollback, partial) is tested.
//
//  The four criteria (R-24):
//   1. reached the scheduler (tasks running) — set once at boot,
//   2. local-control self-test passed — INDEPENDENT of network (a device
//      that updates fine but loses cloud must NOT be rolled back for that),
//   3. network + MQTT re-established,
//   4. explicit confirm written.
//  All four → Confirmed. Window elapses without all four → Rollback.
// =====================================================================
#pragma once

#include <cstdint>
#include "../domain/touch_types.h"   // Millis

namespace ss {

enum class HealthOutcome : uint8_t { Pending = 0, Confirmed = 1, Rollback = 2 };

class OtaHealthConfirm {
public:
    explicit OtaHealthConfirm(Millis windowMs = 180000) : windowMs_(windowMs) {}

    // Call once when the (possibly new) firmware reaches the scheduler.
    void onBoot(Millis now) { bootAt_ = now; reachedScheduler_ = true; started_ = true; }

    // Criterion setters — called as each milestone is reached.
    void markLocalControlOk() { localControlOk_ = true; }
    void markNetworkUp()      { networkUp_ = true; }
    void markExplicitConfirm(){ explicitConfirm_ = true; }

    // Evaluate at time `now`. Only meaningful while this boot is a
    // PENDING-confirmation boot (i.e. an image was just committed). The
    // caller knows that from a KVS "awaiting_confirm" flag.
    HealthOutcome evaluate(Millis now) const {
        if (!started_) return HealthOutcome::Pending;
        if (allCriteriaMet()) return HealthOutcome::Confirmed;
        if (elapsed(now) >= windowMs_) return HealthOutcome::Rollback;
        return HealthOutcome::Pending;
    }

    bool allCriteriaMet() const {
        return reachedScheduler_ && localControlOk_ && networkUp_ && explicitConfirm_;
    }

    // For diagnostics/logging: which criteria are still missing.
    bool needScheduler()  const { return !reachedScheduler_; }
    bool needLocalCtrl()  const { return !localControlOk_; }
    bool needNetwork()    const { return !networkUp_; }
    bool needConfirm()    const { return !explicitConfirm_; }

private:
    Millis elapsed(Millis now) const { return now - bootAt_; }

    Millis windowMs_;
    Millis bootAt_ = 0;
    bool   started_          = false;
    bool   reachedScheduler_ = false;
    bool   localControlOk_   = false;
    bool   networkUp_        = false;
    bool   explicitConfirm_  = false;
};

}  // namespace ss
