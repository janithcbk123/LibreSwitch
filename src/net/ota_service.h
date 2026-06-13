// =====================================================================
//  net/ota_service.h — OtaService (Phase 8 orchestration)
// ---------------------------------------------------------------------
//  Drives the staged, verify-before-commit OTA flow over the seams:
//    PRECHECK  → HW-compat tag (layer 1) + size fits 664KB + anti-
//                downgrade (D8-3). Any fail → reject, NO download.
//    DOWNLOAD  → stream image into the SEPARATE download slot (running
//                app untouched). Exclusive-TLS w/ MQTT (caller yields).
//    VERIFY    → Ed25519 over the staged image (layer 2). Fail → abort
//                (erase slot); running app NEVER affected.
//    COMMIT    → activate staged image; request reboot. Post-boot health
//                confirm (R-24) gates keeping it vs rollBack.
//
//  The whole point: a failed/malicious image lives only in the download
//  slot until PROVEN good, and is erased on any failure. Pure
//  orchestration over seams → host-testable; only the seam impls are
//  on-device. Clean Code: one method per phase, early-return on reject.
// =====================================================================
#pragma once

#include "ota_types.h"
#include "ota_ports.h"
#include "../profiles/profile_resolver.h"
#include "../platform/kv_store.h"

namespace ss {

// Reports phase transitions (→ resp/system/ota over MQTT, Phase 6).
class OtaProgress {
public:
    virtual ~OtaProgress() = default;
    virtual void onPhase(OtaPhase phase, OtaResult result) = 0;
};

class OtaService final : public DownloadSink {
public:
    OtaService(FirmwareWriter& writer, SignatureVerifier& verifier,
               Downloader& downloader, RebootControl& reboot,
               KvStore& kv, const DeviceProfile& profile,
               const FwVersion& currentVersion, OtaProgress& progress)
        : writer_(writer), verifier_(verifier), downloader_(downloader),
          reboot_(reboot), kv_(kv), profile_(profile),
          current_(currentVersion), progress_(progress) {}

    // Entry point for an incoming OTA manifest. Returns the result;
    // on Ok it has requested a reboot into the staged image.
    OtaResult apply(const OtaManifest& m) {
        if (!m.valid) return fail(OtaResult::BadManifest);

        progress_.onPhase(OtaPhase::Precheck, OtaResult::Ok);
        if (OtaResult r = precheck(m); r != OtaResult::Ok) return fail(r);

        progress_.onPhase(OtaPhase::Downloading, OtaResult::Ok);
        if (!doDownload(m)) { writer_.abort(); return fail(OtaResult::DownloadFailed); }

        progress_.onPhase(OtaPhase::Verifying, OtaResult::Ok);
        if (!verifier_.verifyStagedImage(m.sigB64)) {   // VERIFY BEFORE COMMIT
            writer_.abort();                            // erase; app untouched
            return fail(OtaResult::VerifyFailed);
        }

        progress_.onPhase(OtaPhase::Committing, OtaResult::Ok);
        if (!writer_.commit()) { writer_.abort(); return fail(OtaResult::CommitFailed); }

        markAwaitingConfirm(m.version);                 // R-24 gate on next boot
        progress_.onPhase(OtaPhase::Confirmed, OtaResult::Ok);
        reboot_.reboot();
        return OtaResult::Ok;
    }

private:
    // Layer 1 + size + anti-downgrade. No download happens if this fails.
    OtaResult precheck(const OtaManifest& m) {
        if (!ProfileResolver::hwCompatMatches(profile_, m.hwFamily, m.hwChannels))
            return OtaResult::RejectHwMismatch;
        if (!OtaPolicy::sizeFits(m.sizeBytes))
            return OtaResult::RejectTooLarge;
        if (!OtaPolicy::versionAccepted(m.version, current_, m.allowDowngrade))
            return OtaResult::RejectDowngrade;
        return OtaResult::Ok;
    }

    bool doDownload(const OtaManifest& m) {
        if (!writer_.begin(m.sizeBytes)) return false;
        if (!downloader_.download(m.url, m.sizeBytes, *this)) return false;
        return writer_.finish();
    }

    // DownloadSink: each chunk streamed straight to flash (never buffer
    // the whole 664 KB in RAM — stays clear of the ~19 KB TLS floor).
    bool onChunk(const uint8_t* data, size_t len) override {
        return writer_.writeChunk(data, len);
    }

    void markAwaitingConfirm(const FwVersion& v) {
        // Persist the "awaiting health-confirm" flag + the version, so the
        // next boot knows it must confirm-or-rollback (R-24).
        uint8_t flag = 1;
        kv_.setBlob("ota_pending", &flag, 1);
        kv_.setBlob("ota_pending_ver", &v, sizeof(v));
    }

    OtaResult fail(OtaResult r) { progress_.onPhase(OtaPhase::Failed, r); return r; }

    FirmwareWriter&    writer_;
    SignatureVerifier& verifier_;
    Downloader&        downloader_;
    RebootControl&     reboot_;
    KvStore&           kv_;
    const DeviceProfile& profile_;
    FwVersion          current_;
    OtaProgress&       progress_;
};

}  // namespace ss
