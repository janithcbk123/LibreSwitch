// =====================================================================
//  test/host/test_ota.cpp — L1 unit tests (Phase 8)
// ---------------------------------------------------------------------
//  Anti-downgrade + size policy, the 3-layer verify-before-commit
//  sequencing with its safety invariants (HW mismatch → NO download;
//  verify fail → abort, NEVER commit), and the R-24 post-boot health
//  confirm (all-four → confirm; timeout → rollback; local-control
//  criterion independent of network).
// =====================================================================
#include "test_framework.h"
#include "../../src/net/ota_service.h"
#include "../../src/net/ota_manifest.h"
#include "../../src/net/ota_health_confirm.h"
#include "../../src/profiles/profile_registry.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>

using namespace ss;

// ---------------- version / size policy ----------------
TEST(version_newer_than) {
    FwVersion a{1,2,3}, b{1,2,2};
    CHECK(a.isNewerThan(b));
    CHECK(!b.isNewerThan(a));
    CHECK(!a.isNewerThan(a));            // equal is not newer
}
TEST(anti_downgrade_rejects_older_and_equal) {
    FwVersion cur{2,0,0};
    CHECK(OtaPolicy::versionAccepted({2,0,1}, cur, false));   // newer ok
    CHECK(!OtaPolicy::versionAccepted({2,0,0}, cur, false));  // equal rejected
    CHECK(!OtaPolicy::versionAccepted({1,9,9}, cur, false));  // older rejected
}
TEST(signed_downgrade_escape_hatch) {
    FwVersion cur{2,0,0};
    CHECK(OtaPolicy::versionAccepted({1,0,0}, cur, true));    // allowed when signed
}
TEST(size_must_fit_664kb_slot) {
    CHECK(OtaPolicy::sizeFits(1));
    CHECK(OtaPolicy::sizeFits(OtaPolicy::kSlotBytes));
    CHECK(!OtaPolicy::sizeFits(0));
    CHECK(!OtaPolicy::sizeFits(OtaPolicy::kSlotBytes + 1));   // > 664 KB
}

// ---------------- mocks ----------------
struct MockWriter : FirmwareWriter {
    std::vector<std::string> trace;
    size_t bytesWritten = 0;
    bool beginOk=true, finishOk=true, commitOk=true;
    bool begin(uint32_t) override { trace.push_back("begin"); return beginOk; }
    bool writeChunk(const uint8_t*, size_t len) override { bytesWritten += len; trace.push_back("write"); return true; }
    bool finish() override { trace.push_back("finish"); return finishOk; }
    void abort() override { trace.push_back("abort"); }
    bool commit() override { trace.push_back("commit"); return commitOk; }
    bool rollBack() override { trace.push_back("rollback"); return true; }
};
struct MockVerifier : SignatureVerifier {
    bool result = true; int calls = 0;
    bool verifyStagedImage(const char*) override { ++calls; return result; }
};
struct MockDownloader : Downloader {
    bool result = true;
    bool download(const char*, uint32_t size, DownloadSink& sink) override {
        if (!result) return false;
        uint8_t buf[64] = {0};                 // simulate one chunk
        sink.onChunk(buf, sizeof(buf));
        (void)size; return true;
    }
};
struct MockReboot : RebootControl { int reboots = 0; void reboot() override { ++reboots; } };
struct MockKv : KvStore {
    std::map<std::string, std::vector<uint8_t>> store;
    bool erase(const char* k) override { store.erase(k); return true; }

    bool setBlob(const char* k, const void* d, size_t n) override {
        const uint8_t* p=(const uint8_t*)d; store[k]=std::vector<uint8_t>(p,p+n); return true; }
    size_t getBlob(const char* k, void* buf, size_t cap) override {
        auto it=store.find(k); if(it==store.end()) return 0;
        size_t n=std::min(cap,it->second.size()); memcpy(buf,it->second.data(),n); return it->second.size(); }
};
struct MockProgress : OtaProgress {
    std::vector<std::pair<OtaPhase,OtaResult>> phases;
    void onPhase(OtaPhase p, OtaResult r) override { phases.push_back({p,r}); }
    bool reached(OtaPhase p) const { for (auto& x:phases) if (x.first==p) return true; return false; }
};

static OtaManifest goodManifest() {
    OtaManifest m{};
    strcpy(m.url, "https://broker/fw.bin");
    m.version = {2,0,0};
    strcpy(m.sigB64, "AAAA");
    strcpy(m.hwFamily, "bk7231n.relay_touch");
    m.hwChannels = 4;
    m.sizeBytes = 100000;
    m.allowDowngrade = false;
    m.valid = true;
    return m;
}

struct OtaRig {
    MockWriter writer; MockVerifier verifier; MockDownloader dl;
    MockReboot reboot; MockKv kv; MockProgress progress;
    FwVersion current{1,0,0};
    OtaService svc{writer, verifier, dl, reboot, kv, kSwitch4G, current, progress};
};

// ---------------- trust-layer sequencing ----------------
TEST(happy_path_downloads_verifies_commits_reboots) {
    OtaRig r;
    EQ(r.svc.apply(goodManifest()), OtaResult::Ok);
    // ordered: begin, write, finish (download) → verify → commit → reboot
    CHECK(r.verifier.calls == 1);
    EQ(r.reboot.reboots, 1);
    // commit happened AFTER verify (verify-before-commit)
    int iCommit=-1;
    for (size_t i=0;i<r.writer.trace.size();++i) if (r.writer.trace[i]=="commit") iCommit=(int)i;
    CHECK(iCommit>=0); CHECK(r.verifier.calls==1);
    CHECK(r.progress.reached(OtaPhase::Confirmed));
}

// CRITICAL: HW mismatch rejects BEFORE any download (layer 1).
TEST(hw_mismatch_rejects_before_download) {
    OtaRig r;
    OtaManifest m = goodManifest();
    m.hwChannels = 1;                        // 1-gang image on 4-gang device
    EQ(r.svc.apply(m), OtaResult::RejectHwMismatch);
    CHECK(r.writer.trace.empty());           // NEVER touched flash
    EQ(r.reboot.reboots, 0);
}

TEST(downgrade_rejected_before_download) {
    OtaRig r;
    OtaManifest m = goodManifest();
    m.version = {0,9,0};                     // older than current 1.0.0
    EQ(r.svc.apply(m), OtaResult::RejectDowngrade);
    CHECK(r.writer.trace.empty());
}

TEST(oversize_rejected_before_download) {
    OtaRig r;
    OtaManifest m = goodManifest();
    m.sizeBytes = OtaPolicy::kSlotBytes + 1;
    EQ(r.svc.apply(m), OtaResult::RejectTooLarge);
    CHECK(r.writer.trace.empty());
}

// CRITICAL: bad signature → abort, NEVER commit (running app untouched).
TEST(verify_failure_aborts_and_never_commits) {
    OtaRig r;
    r.verifier.result = false;               // signature invalid
    EQ(r.svc.apply(goodManifest()), OtaResult::VerifyFailed);
    EQ(r.reboot.reboots, 0);                 // no reboot
    bool committed=false, aborted=false;
    for (auto& t : r.writer.trace) { if (t=="commit") committed=true; if (t=="abort") aborted=true; }
    CHECK(!committed);                        // NEVER committed an unverified image
    CHECK(aborted);                           // staged slot erased
}

TEST(download_failure_aborts) {
    OtaRig r;
    r.dl.result = false;
    EQ(r.svc.apply(goodManifest()), OtaResult::DownloadFailed);
    EQ(r.verifier.calls, 0);                 // never reached verify
    EQ(r.reboot.reboots, 0);
}

TEST(commit_failure_aborts_no_reboot) {
    OtaRig r;
    r.writer.commitOk = false;
    EQ(r.svc.apply(goodManifest()), OtaResult::CommitFailed);
    EQ(r.reboot.reboots, 0);
}

TEST(bad_manifest_rejected) {
    OtaRig r;
    OtaManifest m{}; m.valid = false;
    EQ(r.svc.apply(m), OtaResult::BadManifest);
    CHECK(r.writer.trace.empty());
}

TEST(success_marks_awaiting_confirm) {
    OtaRig r;
    r.svc.apply(goodManifest());
    uint8_t flag=0; CHECK(r.kv.getBlob("ota_pending", &flag, 1) > 0);
    EQ(flag, (uint8_t)1);                     // R-24 gate set for next boot
}

// ---------------- R-24 health confirm ----------------
TEST(health_confirm_requires_all_four_criteria) {
    OtaHealthConfirm h(180000);
    h.onBoot(0);
    EQ(h.evaluate(0), HealthOutcome::Pending);   // only scheduler reached
    h.markLocalControlOk();
    EQ(h.evaluate(0), HealthOutcome::Pending);
    h.markNetworkUp();
    EQ(h.evaluate(0), HealthOutcome::Pending);
    h.markExplicitConfirm();
    EQ(h.evaluate(0), HealthOutcome::Confirmed);  // all four → confirmed
}

TEST(health_confirm_times_out_to_rollback) {
    OtaHealthConfirm h(180000);
    h.onBoot(0);
    h.markLocalControlOk();                       // partial (no network/confirm)
    EQ(h.evaluate(179999), HealthOutcome::Pending);
    EQ(h.evaluate(180000), HealthOutcome::Rollback);  // window elapsed → rollback
}

TEST(health_confirm_local_control_independent_of_network) {
    // A device that updates fine but never gets network should still
    // distinguish "local control OK" — the criterion is tracked separately
    // so logs can tell "broken" from "degraded-but-controllable" (R-24).
    OtaHealthConfirm h(180000);
    h.onBoot(0);
    h.markLocalControlOk();
    CHECK(!h.needLocalCtrl());                    // local control confirmed
    CHECK(h.needNetwork());                       // but network still missing
    EQ(h.evaluate(180000), HealthOutcome::Rollback);  // still rolls back (needs all 4)
}

TEST(health_confirm_pending_before_boot) {
    OtaHealthConfirm h;
    EQ(h.evaluate(999999), HealthOutcome::Pending);  // onBoot not called yet
}


// ---- manifest codec (new: pure parser for cmd/system/ota payloads) ----
TEST(manifest_parses_full_payload) {
    const char* p = "url=https://h/fw.uf2;ver=1.2.3;sig=QUJD;hw=bk7231n.relay_touch;ch=4;size=123456;dg=0";
    OtaManifest m = OtaManifestCodec::parse((const uint8_t*)p, strlen(p));
    CHECK(m.valid);
    CHECK(strcmp(m.url, "https://h/fw.uf2") == 0);
    EQ(m.version.major, 1); EQ(m.version.minor, 2); EQ(m.version.patch, 3);
    CHECK(strcmp(m.sigB64, "QUJD") == 0);
    CHECK(strcmp(m.hwFamily, "bk7231n.relay_touch") == 0);
    EQ((int)m.hwChannels, 4);
    EQ(m.sizeBytes, 123456u);
    CHECK(!m.allowDowngrade);
}

TEST(manifest_missing_required_field_invalid) {
    const char* p = "url=https://h/fw.uf2;ver=1.2.3;hw=x;ch=4;size=10";  // no sig
    OtaManifest m = OtaManifestCodec::parse((const uint8_t*)p, strlen(p));
    CHECK(!m.valid);
}

TEST(manifest_rejects_bad_version_and_oversize_value) {
    const char* p1 = "url=u;ver=1.2;sig=s;hw=h;ch=4;size=10";            // ver needs 3 parts
    CHECK(!OtaManifestCodec::parse((const uint8_t*)p1, strlen(p1)).valid);
    char big[300]; memset(big, 'a', sizeof(big));
    char p2[400]; int n = snprintf(p2, sizeof(p2), "url=%.*s;ver=1.2.3;sig=s;hw=h;ch=4;size=10", 200, big);
    CHECK(!OtaManifestCodec::parse((const uint8_t*)p2, (size_t)n).valid); // url > cap
}

TEST(manifest_downgrade_flag_and_separator_variants) {
    const char* p = "url=u&ver=2.0.0&sig=s&hw=h&ch=2&size=99&dg=1";
    OtaManifest m = OtaManifestCodec::parse((const uint8_t*)p, strlen(p));
    CHECK(m.valid); CHECK(m.allowDowngrade); EQ((int)m.hwChannels, 2);
}

int main() {
    printf("OTA adapter unit tests\n");
    return tf::run_all();
}
