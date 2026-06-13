// =====================================================================
//  net/ota_types.h — OTA value types, manifest, version policy (pure)
// ---------------------------------------------------------------------
//  Phase 8. The OTA orchestration, manifest parse, HW-compat check, and
//  anti-downgrade comparison are pure + host-testable. The flash write,
//  Ed25519 verify, HTTPS download, and rollBack are behind seams.
//
//  Trust layers (D8-2), all must pass: (1) HW-compat tag precheck,
//  (2) Ed25519 signature verify-before-commit, (3) platform RBL/CRC.
//  Anti-downgrade by default (D8-3) with a signed escape hatch.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "../domain/relay_types.h"   // ChannelId

namespace ss {

// Monotonic firmware version (semantic packed: major.minor.patch).
struct FwVersion {
    uint16_t major = 0, minor = 0, patch = 0;

    // Strictly-greater comparison for anti-downgrade.
    bool isNewerThan(const FwVersion& o) const {
        if (major != o.major) return major > o.major;
        if (minor != o.minor) return minor > o.minor;
        return patch > o.patch;
    }
    bool equals(const FwVersion& o) const {
        return major==o.major && minor==o.minor && patch==o.patch;
    }
};

// OTA command manifest (from cmd/system/ota, Phase 6). Bounded buffers.
struct OtaManifest {
    char       url[160]   = {};
    FwVersion  version{};
    char       sigB64[120]= {};      // Ed25519 signature (base64), 64B→~88 chars
    char       hwFamily[40] = {};
    ChannelId  hwChannels = 0;
    uint32_t   sizeBytes  = 0;
    bool       allowDowngrade = false;   // signed escape hatch (D8-3)
    bool       valid = false;
};

// Result of an OTA attempt — enumerated for logging (Phase 9).
enum class OtaResult : uint8_t {
    Ok               = 0,
    RejectHwMismatch = 1,   // layer 1 fail
    RejectDowngrade  = 2,   // anti-downgrade
    RejectTooLarge   = 3,   // > 664 KB slot
    DownloadFailed   = 4,
    VerifyFailed     = 5,   // layer 2 (Ed25519) fail
    CommitFailed     = 6,
    BadManifest      = 7,
};

// Phases for progress reporting (resp/system/ota, Phase 6/8).
enum class OtaPhase : uint8_t {
    Idle = 0, Precheck, Downloading, Verifying, Committing, Confirmed, Failed
};

// ---- pure policy helpers ----
class OtaPolicy {
public:
    // Anti-downgrade (D8-3): accept only strictly-newer, UNLESS the
    // manifest carries the signed allowDowngrade flag (field recovery).
    static bool versionAccepted(const FwVersion& incoming,
                                const FwVersion& current,
                                bool allowDowngrade) {
        if (allowDowngrade) return true;            // signed escape hatch
        return incoming.isNewerThan(current);       // else strictly newer only
    }

    // Image must fit the FIXED 664 KB download slot (Phase 4 ERRATUM).
    static constexpr uint32_t kSlotBytes = 0xA6000;   // 664 KB
    static bool sizeFits(uint32_t bytes) { return bytes > 0 && bytes <= kSlotBytes; }
};

}  // namespace ss
