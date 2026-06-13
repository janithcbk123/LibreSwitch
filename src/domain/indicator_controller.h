// =====================================================================
//  domain/indicator_controller.h — IndicatorController (Core Domain)
// ---------------------------------------------------------------------
//  Maps high-level DeviceStatus to concrete status-LED blink patterns
//  (GPIO22) and manages the backlight (GPIO23). Driven by a periodic
//  tick(nowMs) from the Control/Health task — pure + time-injected, so
//  every blink pattern is host-testable with synthetic time.
//
//  Staged factory-reset feedback (Q4): while the reset gesture is held,
//  the LED blink accelerates with progress (the familiar "keep holding"
//  feel); at completion it goes solid. setResetProgress()/clearReset()
//  are fed from the TouchInput progress events via the coordinator.
//
//  Clean Code: a small pattern table + one tick stepper; no branches per
//  status sprinkled through the code.
// =====================================================================
#pragma once

#include "indicator_types.h"
#include "result.h"
#include "../ports/indicator_sink.h"

namespace ss {

class IndicatorController {
public:
    explicit IndicatorController(IndicatorSink& sink) : sink_(sink) {}

    // Set the high-level status; the LED pattern follows on subsequent
    // ticks. Changing status resets the blink phase for a clean start.
    void setStatus(DeviceStatus status) {
        if (status != status_) { status_ = status; phaseStart_ = lastTick_; ledOn_ = false; }
    }

    // Backlight control (independent of status LED). PWM honored only if
    // the profile is PWM-capable (Phase 1 A-3); else plain on/off.
    void setBacklight(const BacklightSetting& bl) {
        const uint8_t b = sink_.pwmCapable() ? bl.brightness : 100;
        sink_.setBacklight(bl.on, b);
    }

    // Feed reset-gesture progress (Q4). While active, overrides the status
    // pattern with an accelerating blink. elapsed/total in ms.
    void setResetProgress(Millis elapsedMs, Millis totalMs) {
        resetActive_ = true;
        resetElapsed_ = elapsedMs;
        resetTotal_   = (totalMs == 0) ? 1 : totalMs;   // guard /0
    }
    void clearResetProgress() { resetActive_ = false; }

    // Periodic driver. Call regularly (e.g. every 50 ms) from a task.
    void tick(Millis nowMs) {
        lastTick_ = nowMs;
        const Millis period = currentPeriodMs();
        if (period == 0) { driveLed(solidLevelForStatus()); return; }  // solid
        stepBlink(nowMs, period);
    }

private:
    // Blink period for the active state (ms). 0 == solid (no blink).
    Millis currentPeriodMs() const {
        if (resetActive_) return resetBlinkPeriod();
        switch (status_) {
            case DeviceStatus::Booting:      return 0;     // solid on briefly
            case DeviceStatus::Provisioning: return 500;   // slow, inviting
            case DeviceStatus::Connecting:   return 200;   // quick, "working"
            case DeviceStatus::Online:       return 0;     // solid on = good
            case DeviceStatus::Offline:      return 2000;  // rare heartbeat
            case DeviceStatus::Fault:        return 100;   // fast = attention
            case DeviceStatus::FactoryReset: return resetBlinkPeriod();
        }
        return 0;
    }

    // Reset feedback: blink speeds up as the hold nears completion, then
    // goes solid at/after completion (the "almost there → done" feel).
    Millis resetBlinkPeriod() const {
        if (resetElapsed_ >= resetTotal_) return 0;            // solid = done
        // map progress 0..1 → period 600ms (slow) .. 80ms (frantic)
        const uint32_t pct = (resetElapsed_ * 100) / resetTotal_;   // 0..99
        const Millis slow = 600, fast = 80;
        return slow - ((slow - fast) * pct) / 100;
    }

    bool solidLevelForStatus() const {
        if (resetActive_ && resetElapsed_ >= resetTotal_) return true; // done = solid on
        switch (status_) {
            case DeviceStatus::Booting: return true;
            case DeviceStatus::Online:  return true;
            default:                    return true;  // any solid state = LED on
        }
    }

    void stepBlink(Millis nowMs, Millis period) {
        const Millis half = period / 2;
        const Millis inCycle = (nowMs - phaseStart_) % period;
        driveLed(inCycle < half);
    }

    void driveLed(bool on) {
        if (on != ledOn_) { ledOn_ = on; sink_.setStatusLed(on); }
    }

    IndicatorSink& sink_;
    DeviceStatus   status_     = DeviceStatus::Booting;
    Millis         phaseStart_ = 0;
    Millis         lastTick_   = 0;
    bool           ledOn_      = false;

    bool   resetActive_  = false;
    Millis resetElapsed_ = 0;
    Millis resetTotal_   = 1;
};

}  // namespace ss
