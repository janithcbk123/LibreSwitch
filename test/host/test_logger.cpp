// =====================================================================
//  test/host/test_logger.cpp — L1 unit tests (Phase 9)
// ---------------------------------------------------------------------
//  Record codec (20 B layout + roundtrip), dual-clock behavior (pre-NTP
//  → floor + clock_valid=0; post-NTP → real UTC + clock_valid=1), NTP
//  anchor emission (+ idempotence), boot-id increment/persistence, and
//  the last-known-NTP floor.
// =====================================================================
#include "test_framework.h"
#include "../../src/log/logger.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>

using namespace ss;

// ---------------- codec ----------------
TEST(record_is_20_bytes) {
    EQ(sizeof(LogRecord), (size_t)20);
}
TEST(record_roundtrips) {
    LogRecord r{};
    r.ts_utc = 1700000000u; r.uptime_ms = 12345; r.boot_id = 7;
    r.code = (uint16_t)EventCode::RelayOn; r.arg1 = 2; r.arg2 = 99;
    r.severity = (uint8_t)Severity::Info; r.flags = kFlagClockValid;
    uint8_t buf[20]; LogCodec::encode(r, buf);
    LogRecord out{};
    CHECK(LogCodec::decode(buf, 20, out));
    EQ(out.ts_utc, r.ts_utc); EQ(out.uptime_ms, r.uptime_ms);
    EQ(out.boot_id, r.boot_id); EQ(out.code, r.code);
    EQ(out.arg1, r.arg1); EQ(out.arg2, r.arg2);
    EQ(out.severity, r.severity); EQ(out.flags, r.flags);
}
TEST(decode_rejects_wrong_length) {
    uint8_t buf[10] = {0}; LogRecord out{};
    CHECK(!LogCodec::decode(buf, 10, out));
}

// ---------------- mocks ----------------
struct MockSink : TsdbSink {
    std::vector<LogRecord> records;
    std::vector<uint32_t> sortKeys;
    bool append(const uint8_t* rec, size_t len, uint32_t sortKey) override {
        LogRecord r{}; if (!LogCodec::decode(rec, len, r)) return false;
        records.push_back(r); sortKeys.push_back(sortKey); return true;
    }
    int countCode(EventCode c) const {
        int n=0; for (auto& r:records) if (r.code==(uint16_t)c) ++n; return n;
    }
    const LogRecord& last() const { return records.back(); }
};
struct MockTime : TimeSource {
    uint32_t up = 1000; uint32_t utc = 0; bool synced = false;
    uint32_t uptimeMs() override { return up; }
    uint32_t utcEpoch() override { return utc; }
    bool ntpSynced() override { return synced; }
};
struct MockKv : KvStore {
    std::map<std::string, std::vector<uint8_t>> store;
    bool erase(const char* k) override { store.erase(k); return true; }

    bool setBlob(const char* k, const void* d, size_t n) override {
        const uint8_t* p=(const uint8_t*)d; store[k]=std::vector<uint8_t>(p,p+n); return true; }
    size_t getBlob(const char* k, void* buf, size_t cap) override {
        auto it=store.find(k); if(it==store.end()) return 0;
        size_t n=std::min(cap,it->second.size()); memcpy(buf,it->second.data(),n); return it->second.size(); }
};

struct Rig {
    MockSink sink; MockTime time; MockKv kv;
    Logger log{sink, time, kv};
};

// ---------------- boot / dual-clock ----------------
TEST(begin_emits_boot_and_increments_boot_id) {
    Rig r;
    r.log.begin();
    CHECK(r.sink.countCode(EventCode::Boot) == 1);
    // boot_id persisted as 1 on first boot
    uint16_t id=0; r.kv.getBlob("boot_id", &id, sizeof(id));
    EQ(id, (uint16_t)1);
    EQ(r.sink.last().boot_id, (uint16_t)1);
}

TEST(boot_id_increments_across_sessions) {
    MockKv kv;
    { MockSink s; MockTime t; Logger l(s,t,kv); l.begin(); }   // boot 1
    { MockSink s; MockTime t; Logger l(s,t,kv); l.begin(); }   // boot 2
    uint16_t id=0; kv.getBlob("boot_id", &id, sizeof(id));
    EQ(id, (uint16_t)2);
}

TEST(pre_ntp_record_has_no_clock_valid_and_uses_floor) {
    Rig r;
    r.time.synced = false;
    uint32_t floorSeed = 1600000000u;
    r.kv.setBlob("ntp_floor", &floorSeed, sizeof(floorSeed));
    r.log.begin();                            // loads floor
    r.time.up = 5000;
    r.log.log(EventCode::RelayOn, Severity::Info, 1, 0);
    const LogRecord& rec = r.sink.last();
    EQ(rec.uptime_ms, (uint32_t)5000);        // uptime always valid
    CHECK((rec.flags & kFlagClockValid) == 0); // not real wall-clock
    EQ(rec.ts_utc, (uint32_t)1600000000u);     // coarse floor used
}

TEST(post_ntp_record_has_clock_valid_and_real_utc) {
    Rig r;
    r.log.begin();
    r.time.synced = true; r.time.utc = 1700000123u; r.time.up = 8000;
    r.log.log(EventCode::MqttConnected, Severity::Info);
    const LogRecord& rec = r.sink.last();
    CHECK((rec.flags & kFlagClockValid) != 0);
    EQ(rec.ts_utc, (uint32_t)1700000123u);
    EQ(rec.uptime_ms, (uint32_t)8000);
}

TEST(uptime_is_the_tsdb_sort_key) {
    Rig r;
    r.log.begin();
    r.time.up = 4242;
    r.log.log(EventCode::TouchEvent, Severity::Debug);
    EQ(r.sink.sortKeys.back(), (uint32_t)4242);  // ordered by uptime (RTC-less)
}

// ---------------- NTP anchor + floor ----------------
TEST(ntp_sync_emits_anchor_with_both_clocks) {
    Rig r;
    r.log.begin();
    r.time.synced = true; r.time.utc = 1700001000u; r.time.up = 30000;
    r.log.onNtpSync();
    CHECK(r.sink.countCode(EventCode::NtpSync) == 1);
    const LogRecord& a = r.sink.last();
    CHECK((a.flags & kFlagAnchor) != 0);       // anchor flag set
    CHECK((a.flags & kFlagClockValid) != 0);   // real UTC at anchor
    EQ(a.ts_utc, (uint32_t)1700001000u);
    EQ(a.uptime_ms, (uint32_t)30000);          // both clocks captured
}

TEST(ntp_sync_is_idempotent_only_first_anchors) {
    Rig r;
    r.log.begin();
    r.time.synced = true; r.time.utc = 1700001000u;
    r.log.onNtpSync();
    r.log.onNtpSync();                          // second call
    EQ(r.sink.countCode(EventCode::NtpSync), 1); // only one anchor
}

TEST(ntp_sync_persists_floor_for_next_boot) {
    MockKv kv;
    {
        MockSink s; MockTime t; Logger l(s,t,kv); l.begin();
        t.synced = true; t.utc = 1700002000u;
        l.onNtpSync();                          // persists floor
    }
    uint32_t floor=0; kv.getBlob("ntp_floor", &floor, sizeof(floor));
    EQ(floor, (uint32_t)1700002000u);
    // next boot: pre-NTP record should use the persisted floor
    MockSink s2; MockTime t2; Logger l2(s2,t2,kv); l2.begin();
    t2.synced = false; t2.up = 100;
    l2.log(EventCode::WifiUp, Severity::Info);
    EQ(s2.last().ts_utc, (uint32_t)1700002000u);  // floor carried across boot
}

int main() {
    printf("Logger (Phase 9) unit tests\n");
    return tf::run_all();
}
