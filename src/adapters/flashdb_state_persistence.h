// =====================================================================
//  adapters/flashdb_state_persistence.h — relay-state persistence adapter
// ---------------------------------------------------------------------
//  Implements the domain StatePersistencePort over the KvStore seam
//  (FlashDB KVS @ Phase 4 partition 0x1D8000). Serializes the last-known
//  relay state as a VERSIONED, CRC-CHECKED record so that corrupt,
//  partial, or absent data reads back as valid=false — which feeds
//  BootPolicy's R-7 safe fallback (boot OFF rather than restore garbage).
//
//  Serialization + version + CRC + validity logic is host-testable with
//  a RAM-backed mock KvStore; only the FlashDB-backed KvStore impl needs
//  the device. Clean Code: one pack fn, one unpack fn, single key.
// =====================================================================
#pragma once

#include "../ports/state_persistence_port.h"
#include "../platform/kv_store.h"
#include "../domain/crc16.h"
#include "../domain/config.h"
#include <cstring>

namespace ss {

class FlashDbStatePersistence final : public StatePersistencePort {
public:
    explicit FlashDbStatePersistence(KvStore& kv) : kv_(kv) {}

    PersistedRelayState loadRelayState() override {
        Record rec{};
        const size_t n = kv_.getBlob(kKey, &rec, sizeof(rec));
        PersistedRelayState out{};                      // valid=false default
        if (n != sizeof(rec))            return out;     // absent/short → invalid
        if (rec.magic != kMagic)         return out;     // wrong record
        if (rec.version != kVersion)     return out;     // version mismatch
        if (rec.crc != computeCrc(rec))  return out;     // corrupt → invalid
        for (ChannelId ch = 0; ch < kMaxChannels; ++ch)
            out.channel[ch] = (rec.state[ch] != 0) ? RelayState::On : RelayState::Off;
        out.valid = true;
        return out;
    }

    Status saveRelayState(const PersistedRelayState& s) override {
        Record rec{};
        rec.magic   = kMagic;
        rec.version = kVersion;
        for (ChannelId ch = 0; ch < kMaxChannels; ++ch)
            rec.state[ch] = (s.channel[ch] == RelayState::On) ? 1 : 0;
        rec.crc = computeCrc(rec);
        return kv_.setBlob(kKey, &rec, sizeof(rec))
                   ? Status::success()
                   : Status::fail(Error::InternalInvariant);
    }

private:
    // On-flash record. Fixed layout; magic+version+crc guard integrity.
    struct Record {
        uint32_t magic;
        uint8_t  version;
        uint8_t  state[kMaxChannels];   // 0/1 per channel
        uint8_t  _pad;                  // keep crc on a clean boundary
        uint16_t crc;                   // CRC over all preceding bytes
    };

    static uint16_t computeCrc(const Record& r) {
        // CRC covers everything except the crc field itself.
        const size_t lenSansCrc = offsetof(Record, crc);
        return crc16(&r, lenSansCrc);
    }

    static constexpr const char* kKey  = "relay_state";
    static constexpr uint32_t kMagic   = 0x52535431;   // 'RST1'
    static constexpr uint8_t  kVersion = 1;

    KvStore& kv_;
};

}  // namespace ss
