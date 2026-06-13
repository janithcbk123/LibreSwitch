// =====================================================================
//  platform/kv_store.h — key-value store seam (Platform layer)
// ---------------------------------------------------------------------
//  The thin seam over FlashDB's KVS (armink/FlashDB). Adapters serialize
//  to/from blobs and call this; the FlashDB-backed impl is on-device
//  only, so serialization + validation logic stays host-testable with a
//  RAM-backed mock. Verified FlashDB API: fdb_kv_set_blob / fdb_kv_get_blob
//  (returns read size; 0 == key absent) / fdb_blob_make.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>

namespace ss {

class KvStore {
public:
    virtual ~KvStore() = default;

    // Store `len` bytes under `key`. Returns true on success.
    virtual bool setBlob(const char* key, const void* data, size_t len) = 0;

    // Read up to `bufLen` bytes for `key` into `buf`. Returns the number
    // of bytes actually read; 0 means the key is absent (FlashDB returns
    // 0 read size for a missing key — the basis for absent-detection).
    virtual size_t getBlob(const char* key, void* buf, size_t bufLen) = 0;

    // Delete `key`. Returns true if removed (or already absent is fine).
    // Default no-op=false so existing impls/mocks keep compiling; real
    // stores override. Used by factory reset (wipe creds, keep profile).
    virtual bool erase(const char* /*key*/) { return false; }
};

}  // namespace ss
