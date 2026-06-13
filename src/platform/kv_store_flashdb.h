// =====================================================================
//  platform/kv_store_flashdb.h — FlashDB KvStore impl (ON-DEVICE ONLY)
// ---------------------------------------------------------------------
//  The ONE place that calls FlashDB's KVS. Compiled only on-device
//  (guarded), never in host tests — the host suite uses a RAM mock.
//  Correctness of the FlashDB binding itself is proven on-device.
//
//  Verified FlashDB API (armink/FlashDB):
//    fdb_kvdb_init(db, name, partition, default_kv, user_data)
//    fdb_blob_make(&blob, buf, len)
//    fdb_kv_set_blob(db, key, &blob)
//    fdb_kv_get_blob(db, key, &blob) -> returns read size (0 == absent)
//  The KVS lives in the Phase 4 partition "kvs" @ 0x1D8000 (32 KB),
//  registered with FlashDB's FAL layer at init (board partition table).
// =====================================================================
#pragma once

#include "kv_store.h"

#if defined(LT_BUILD) || defined(FLASHDB_USING)
#include <flashdb.h>

namespace ss {

class FlashDbKvStore final : public KvStore {
public:
    // partition must match the FAL partition name for the Phase 4 KVS
    // region (0x1D8000, 32 KB). Returns false if FlashDB init fails.
    bool begin(const char* partition = "kvs") {
        if (ready_) return true;                    // idempotent: already bound
        fdb_err_t err = fdb_kvdb_init(&db_, "env", partition, nullptr, nullptr);
        ready_ = (err == FDB_NO_ERR);
        return ready_;
    }

    bool setBlob(const char* key, const void* data, size_t len) override {
        if (!ready_) return false;
        struct fdb_blob blob;
        fdb_blob_make(&blob, data, len);
        return fdb_kv_set_blob(&db_, key, &blob) == FDB_NO_ERR;
    }

    bool erase(const char* key) override {
        if (!ready_) return false;
        return fdb_kv_del(&db_, key) == FDB_NO_ERR;
    }

    size_t getBlob(const char* key, void* buf, size_t bufLen) override {
        if (!ready_) return 0;
        struct fdb_blob blob;
        fdb_blob_make(&blob, buf, bufLen);
        return fdb_kv_get_blob(&db_, key, &blob);   // 0 == absent
    }

private:
    struct fdb_kvdb db_ {};
    bool ready_ = false;
};

}  // namespace ss

#endif  // LT_BUILD || FLASHDB_USING
