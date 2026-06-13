// =====================================================================
//  log/logger.h — Logger (Phase 9 dual-clock event logger)
// ---------------------------------------------------------------------
//  Assembles dual-clock records and appends them to the TSDB ring.
//  Handles the RTC-less timestamp strategy (D9-2):
//   • every record carries uptime_ms (always valid) + ts_utc (real iff
//     NTP-synced this session, with the clock_valid flag),
//   • on the FIRST NTP sync it emits an ANCHOR event (NtpSync) carrying
//     both clocks, so the reader can back-compute wall-clock for earlier
//     records,
//   • persists the last-known NTP epoch to KVS so a later boot has a
//     coarse "time ≥ X" floor before re-sync (marked low-confidence —
//     clock_valid stays 0 until a real sync this session).
//
//  boot_id (from a KVS counter) stitches sessions across reboots. Pure
//  logic over seams → host-testable; the TSDB/fdb calls are on-device.
// =====================================================================
#pragma once

#include "log_types.h"
#include "log_ports.h"
#include "../platform/kv_store.h"

namespace ss {

class Logger {
public:
    Logger(TsdbSink& sink, TimeSource& time, KvStore& kv)
        : sink_(sink), time_(time), kv_(kv) {}

    // Call once at startup: bumps + loads the boot counter, loads the
    // last-known-NTP floor, and emits a Boot event.
    void begin() {
        bootId_ = nextBootId();
        loadNtpFloor();
        log(EventCode::Boot, Severity::Info, bootId_, 0);
    }

    // Core logging entry. Builds a dual-clock record and appends it.
    bool log(EventCode code, Severity sev, uint16_t arg1 = 0, uint16_t arg2 = 0) {
        return appendRecord(buildRecord(code, sev, arg1, arg2, /*anchor*/ false));
    }

    // Call when NTP first synchronizes this session. Emits the anchor and
    // persists the floor. Idempotent: only the first sync anchors.
    void onNtpSync() {
        if (anchored_) return;
        anchored_ = true;
        LogRecord anchor = buildRecord(EventCode::NtpSync, Severity::Info,
                                       0, 0, /*anchor*/ true);
        appendRecord(anchor);
        persistNtpFloor(time_.utcEpoch());
    }

private:
    LogRecord buildRecord(EventCode code, Severity sev,
                          uint16_t arg1, uint16_t arg2, bool anchor) {
        LogRecord r{};
        r.uptime_ms = time_.uptimeMs();          // ALWAYS valid
        r.boot_id   = bootId_;
        r.code      = static_cast<uint16_t>(code);
        r.severity  = static_cast<uint8_t>(sev);
        r.arg1 = arg1; r.arg2 = arg2;
        if (time_.ntpSynced()) {
            r.ts_utc = time_.utcEpoch();
            r.flags |= kFlagClockValid;          // real wall-clock
        } else {
            r.ts_utc = ntpFloor_;                // coarse floor (low-confidence)
            // clock_valid stays 0 — reader treats as approximate
        }
        if (anchor) r.flags |= kFlagAnchor;
        return r;
    }

    bool appendRecord(const LogRecord& r) {
        uint8_t buf[sizeof(LogRecord)];
        LogCodec::encode(r, buf);
        // uptime is the TSDB sort key → ordering holds without an RTC.
        return sink_.append(buf, sizeof(buf), r.uptime_ms);
    }

    uint16_t nextBootId() {
        uint16_t id = 0;
        kv_.getBlob("boot_id", &id, sizeof(id));   // 0 if absent
        ++id;
        kv_.setBlob("boot_id", &id, sizeof(id));
        return id;
    }

    void loadNtpFloor() {
        ntpFloor_ = 0;
        kv_.getBlob("ntp_floor", &ntpFloor_, sizeof(ntpFloor_));
    }
    void persistNtpFloor(uint32_t epoch) {
        if (epoch == 0) return;
        ntpFloor_ = epoch;
        kv_.setBlob("ntp_floor", &epoch, sizeof(epoch));
    }

    TsdbSink&  sink_;
    TimeSource& time_;
    KvStore&   kv_;
    uint16_t   bootId_   = 0;
    uint32_t   ntpFloor_ = 0;
    bool       anchored_ = false;
};

}  // namespace ss
