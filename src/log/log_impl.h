// =====================================================================
//  log/log_impl.h — on-device logging seam impls (ON-DEVICE)
// ---------------------------------------------------------------------
//  PLATFORM CONSTRAINT (verified at build): the LibreTiny-bundled
//  library-flashdb (1.2.0) is compiled KVDB-ONLY — its fdb_cfg.h has
//  `// #define FDB_USING_TSDB` commented out, so fdb_tsl_* are NOT
//  available, and FDB_WRITE_GRAN is 8 (not 1). We therefore implement
//  the log ring over the KVDB API (which IS available) rather than TSDB.
//
//  Design (unchanged behaviour behind the TsdbSink seam): a KVDB ring in
//  the "userdata" partition (0x1E3000, 116 KB). Records are stored under
//  rotating keys "e00000".."eNNNNN"; a "loghead" key holds the next slot
//  index, wrapping at kRingSlots → rollover (D9-3). The Logger and all
//  its host tests are unchanged — only this on-device impl differs.
//
//  This is the honest adaptation to what the platform actually ships: no
//  TSDB, so a KVDB ring. Ordering is preserved by the slot index + the
//  record's own boot_id/uptime (the reader reconstructs, as designed).
//
//  Verified API: fdb_kvdb_init(db,name,partition,default_kv,user_data);
//  fdb_kv_set_blob / fdb_kv_get_blob ; fdb_blob_make.
// =====================================================================
#pragma once

#include "log_ports.h"

#if defined(LT_BUILD) || defined(FLASHDB_USING)
#include <flashdb.h>
#include <Arduino.h>
#include <sys/time.h>
#include <cstdio>

namespace ss {

class FlashDbTsdbSink final : public TsdbSink {
public:
    // ~116 KB / (20 B record + KVDB per-key overhead ~ a few sectors of
    // headroom) — keep a conservative ring size well within the partition.
    static constexpr uint32_t kRingSlots = 2048;

    bool begin(const char* partition = "userdata") {
        fdb_err_t err = fdb_kvdb_init(&db_, "log", partition, nullptr, nullptr);
        if (err != FDB_NO_ERR) { ready_ = false; return false; }
        loadHead();
        ready_ = true;
        return true;
    }

    bool append(const uint8_t* record, size_t len, uint32_t /*sortKey*/) override {
        if (!ready_) return false;
        char key[12];
        snprintf(key, sizeof(key), "e%05lu", static_cast<unsigned long>(head_));
        struct fdb_blob blob;
        fdb_blob_make(&blob, record, len);
        if (fdb_kv_set_blob(&db_, key, &blob) != FDB_NO_ERR) return false;
        head_ = (head_ + 1) % kRingSlots;       // rotate → rollover (D9-3)
        saveHead();
        return true;
    }

private:
    void loadHead() {
        head_ = 0;
        struct fdb_blob blob;
        uint32_t v = 0;
        fdb_blob_make(&blob, &v, sizeof(v));
        if (fdb_kv_get_blob(&db_, "loghead", &blob) == sizeof(v)) head_ = v % kRingSlots;
    }
    void saveHead() {
        struct fdb_blob blob;
        fdb_blob_make(&blob, &head_, sizeof(head_));
        fdb_kv_set_blob(&db_, "loghead", &blob);
    }

    struct fdb_kvdb db_ {};
    uint32_t head_ = 0;
    bool ready_ = false;
};

// Dual-clock time source: uptime from millis() (always), UTC from the
// system clock once SNTP has set it (the R-0-proven NTP path).
class SystemTimeSource final : public TimeSource {
public:
    uint32_t uptimeMs() override { return static_cast<uint32_t>(millis()); }
    uint32_t utcEpoch() override {
        // Use gettimeofday (routed through the platform's __wrap_gettimeofday)
        // rather than newlib time(), which would drag libnosys's _gettimeofday
        // into the link and clash with the platform's definition.
        struct timeval tv;
        if (gettimeofday(&tv, nullptr) != 0) return 0;
        return (tv.tv_sec > kPlausibleEpoch) ? static_cast<uint32_t>(tv.tv_sec) : 0;
    }
    bool ntpSynced() override { return utcEpoch() != 0; }
private:
    static constexpr long kPlausibleEpoch = 1672531200;   // 2023-01-01
};

}  // namespace ss

#endif  // LT_BUILD || FLASHDB_USING
