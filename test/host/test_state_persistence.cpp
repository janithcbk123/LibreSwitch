// =====================================================================
//  test/host/test_state_persistence.cpp — L1 unit/integration tests
// ---------------------------------------------------------------------
//  Versioned, CRC-checked relay-state persistence over a RAM mock
//  KvStore: round-trip, absent→invalid, corruption→invalid, version/
//  magic mismatch→invalid, short-read→invalid, plus a full BootPolicy
//  integration proving corrupt state drives a SAFE-OFF boot (R-7).
// =====================================================================
#include "test_framework.h"
#include "../../src/adapters/flashdb_state_persistence.h"
#include "../../src/domain/boot_policy.h"
#include "../../src/domain/relay_controller.h"
#include "../../src/domain/crc16.h"
#include <map>
#include <vector>
#include <string>

using namespace ss;

// RAM-backed KvStore mock; supports corruption injection.
struct MockKv : KvStore {
    std::map<std::string, std::vector<uint8_t>> store;
    bool erase(const char* k) override { store.erase(k); return true; }

    bool setBlob(const char* key, const void* data, size_t len) override {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        store[key] = std::vector<uint8_t>(p, p + len);
        return true;
    }
    size_t getBlob(const char* key, void* buf, size_t bufLen) override {
        auto it = store.find(key);
        if (it == store.end()) return 0;                 // absent
        const size_t n = std::min(bufLen, it->second.size());
        std::memcpy(buf, it->second.data(), n);
        return it->second.size();                        // real saved size
    }
};

struct RelaySinkMock : RelaySink { bool driveRelay(ChannelId, RelayState) override { return true; } };

// ---- CRC16 sanity ----
TEST(crc16_detects_single_bit_change) {
    uint8_t a[4] = {1, 2, 3, 4};
    uint8_t b[4] = {1, 2, 3, 5};
    CHECK(crc16(a, 4) != crc16(b, 4));
}
TEST(crc16_is_deterministic) {
    uint8_t a[3] = {9, 8, 7};
    EQ(crc16(a, 3), crc16(a, 3));
}

// ---- persistence round-trip ----
TEST(save_then_load_roundtrips_state) {
    MockKv kv; FlashDbStatePersistence p(kv);
    PersistedRelayState in{};
    in.channel[0] = RelayState::On;
    in.channel[1] = RelayState::Off;
    in.channel[2] = RelayState::On;
    in.channel[3] = RelayState::On;
    EQ(p.saveRelayState(in).error, Error::None);

    PersistedRelayState out = p.loadRelayState();
    CHECK(out.valid);
    EQ(out.channel[0], RelayState::On);
    EQ(out.channel[1], RelayState::Off);
    EQ(out.channel[2], RelayState::On);
    EQ(out.channel[3], RelayState::On);
}

TEST(load_absent_key_is_invalid) {
    MockKv kv; FlashDbStatePersistence p(kv);
    PersistedRelayState out = p.loadRelayState();   // nothing saved
    CHECK(!out.valid);
}

TEST(corrupted_record_reads_back_invalid) {
    MockKv kv; FlashDbStatePersistence p(kv);
    PersistedRelayState in{}; in.channel[0] = RelayState::On;
    p.saveRelayState(in);
    // flip a byte in the stored blob → CRC must fail
    auto& blob = kv.store["relay_state"];
    blob[5] ^= 0xFF;
    PersistedRelayState out = p.loadRelayState();
    CHECK(!out.valid);                              // corruption caught
}

TEST(wrong_magic_reads_back_invalid) {
    MockKv kv; FlashDbStatePersistence p(kv);
    PersistedRelayState in{}; p.saveRelayState(in);
    kv.store["relay_state"][0] ^= 0xFF;             // break magic
    CHECK(!p.loadRelayState().valid);
}

TEST(short_record_reads_back_invalid) {
    MockKv kv; FlashDbStatePersistence p(kv);
    PersistedRelayState in{}; p.saveRelayState(in);
    kv.store["relay_state"].pop_back();             // truncate → size mismatch
    CHECK(!p.loadRelayState().valid);
}

// ---- R-7 integration: corrupt persisted state → safe-OFF boot ----
TEST(bootpolicy_restorelast_with_corruption_boots_off) {
    MockKv kv; FlashDbStatePersistence p(kv);
    // persist all-ON, then corrupt it
    PersistedRelayState in{};
    for (auto& c : in.channel) c = RelayState::On;
    p.saveRelayState(in);
    kv.store["relay_state"][6] ^= 0xFF;             // corrupt

    RelaySinkMock sink; RelayController rc(sink); rc.init(4);
    BootPolicy boot(rc, p);
    EQ(boot.applyBootState(RestorePolicy::RestoreLast).error, Error::None);
    for (ChannelId ch = 0; ch < 4; ++ch)
        EQ(rc.state(ch), RelayState::Off);          // SAFE fallback, not garbage-ON
}

TEST(bootpolicy_restorelast_with_valid_state_restores) {
    MockKv kv; FlashDbStatePersistence p(kv);
    PersistedRelayState in{};
    in.channel[0] = RelayState::On; in.channel[2] = RelayState::On;
    p.saveRelayState(in);

    RelaySinkMock sink; RelayController rc(sink); rc.init(4);
    BootPolicy boot(rc, p);
    boot.applyBootState(RestorePolicy::RestoreLast);
    EQ(rc.state(0), RelayState::On);
    EQ(rc.state(1), RelayState::Off);
    EQ(rc.state(2), RelayState::On);
}

int main() {
    printf("State persistence + CRC unit/integration tests\n");
    return tf::run_all();
}
