// =====================================================================
//  net/provisioning_service.h — ProvisioningService (Phase 7 state machine)
// ---------------------------------------------------------------------
//  Orchestrates: AP_PORTAL → (form) → TESTING_CREDS (sequential) →
//  IDENTITY_BOOTSTRAP → OPERATIONAL, with failure returning to AP_PORTAL.
//  Drives WifiControl + PortalServer + Enrollment through seams and
//  persists creds via KvStore. Pure logic + time-injected → host-testable.
//
//  Invariants enforced here:
//   • Sequential test (D7-3): AP is torn down BEFORE the STA attempt and
//     restored on failure — never concurrent (beken limitation, verified).
//   • Creds are persisted ONLY after a successful STA join (don't store
//     bad creds that would brick into a silent non-connecting state).
//   • One-time enrollment token (R-19) is passed to Enrollment; identity
//     bootstrap only runs after creds are validated.
//   • AP idle-timeout (R-20): if left unconfigured for N ms, the service
//     reports timeout (caller can drop the AP to shrink the attack window).
// =====================================================================
#pragma once

#include "provisioning_types.h"
#include "provisioning_ports.h"
#include "../platform/kv_store.h"
#include "../domain/result.h"
#include "../domain/touch_types.h"   // Millis

namespace ss {

struct ProvConfig {
    const char* apSsid;            // per-device, e.g. "Switch-A1B2"
    const char* apPass;            // per-device default (R-20)
    uint32_t    staTimeoutMs = 20000;
    uint32_t    apIdleTimeoutMs = 600000;  // 10 min unconfigured → timeout
};

class ProvisioningService final : public PortalSubmissionHandler {
public:
    ProvisioningService(WifiControl& wifi, PortalServer& portal,
                        Enrollment& enroll, KvStore& kv, const ProvConfig& cfg)
        : wifi_(wifi), portal_(portal), enroll_(enroll), kv_(kv), cfg_(cfg) {}

    // Entry: if already provisioned (creds present), go straight to STA;
    // else open the portal. Returns the entered state.
    ProvState begin(Millis now) {
        startedAt_ = now;
        if (hasStoredCreds() && enroll_.hasOperationalCert()) {
            return tryStoredConnect() ? enter(ProvState::Operational, now)
                                      : openPortal(now);
        }
        return openPortal(now);
    }

    // Periodic driver. Services the portal and enforces the idle timeout.
    void tick(Millis now) {
        if (state_ == ProvState::ApPortal) {
            portal_.poll();
            if (timedOut(now)) idleTimedOut_ = true;   // caller may act on this
        }
    }

    // PortalSubmissionHandler: a form came in → run the sequential test.
    void onCredentialsSubmitted(const ProvisioningCredentials& creds) override {
        if (!creds.valid) { portal_.setMessage("Invalid: SSID required"); return; }
        pending_ = creds;
        runSequentialTest();
    }

    ProvState state() const { return state_; }
    bool idleTimedOut() const { return idleTimedOut_; }

private:
    ProvState openPortal(Millis now) {
        wifi_.startAp(cfg_.apSsid, cfg_.apPass);   // WPA2 per-device (R-20)
        portal_.start(*this);
        return enter(ProvState::ApPortal, now);
    }

    // The D7-3 sequential dance: AP down → STA try → on fail restore AP.
    void runSequentialTest() {
        enter(ProvState::TestingCreds, startedAt_);
        portal_.setMessage("Testing… the switch will leave this network.");
        portal_.stop();
        wifi_.stopAp();                              // AP DOWN before STA

        const bool joined = wifi_.connectSta(pending_.ssid, pending_.pass,
                                             cfg_.staTimeoutMs);
        if (!joined) { onTestFailed(); return; }

        persistCreds(pending_);                      // store ONLY after success
        if (!runEnrollment()) { onTestFailed(); return; }
        state_ = ProvState::Operational;
    }

    bool runEnrollment() {
        state_ = ProvState::IdentityBootstrap;
        return enroll_.enroll(pending_.hasToken() ? pending_.token : nullptr);
    }

    void onTestFailed() {
        wifi_.disconnectSta();
        state_ = ProvState::Failed;
        openPortal(startedAt_);                      // restore AP, let user retry
        portal_.setMessage("Could not connect. Check password and retry.");
    }

    // ---- persistence (creds → KVS, consumed later by bringUpMqtt) ----
    void persistCreds(const ProvisioningCredentials& c) {
        kv_.setBlob("sta_ssid", c.ssid, strlen(c.ssid) + 1);
        kv_.setBlob("sta_pass", c.pass, strlen(c.pass) + 1);
        // token is one-time: not persisted long-term; used for enroll only.
    }

    bool hasStoredCreds() {
        char ssid[kMaxSsid];
        return kv_.getBlob("sta_ssid", ssid, sizeof(ssid)) > 0;
    }

    bool tryStoredConnect() {
        char ssid[kMaxSsid]{}, pass[kMaxPass]{};
        if (kv_.getBlob("sta_ssid", ssid, sizeof(ssid)) == 0) return false;
        kv_.getBlob("sta_pass", pass, sizeof(pass));
        return wifi_.connectSta(ssid, pass, cfg_.staTimeoutMs);
    }

    ProvState enter(ProvState s, Millis now) { state_ = s; stateSince_ = now; return s; }
    bool timedOut(Millis now) const {
        return static_cast<uint32_t>(now - startedAt_) >= cfg_.apIdleTimeoutMs;
    }

    WifiControl&  wifi_;
    PortalServer& portal_;
    Enrollment&   enroll_;
    KvStore&      kv_;
    ProvConfig    cfg_;

    ProvState  state_       = ProvState::ApPortal;
    Millis     startedAt_   = 0;
    Millis     stateSince_  = 0;
    bool       idleTimedOut_= false;
    ProvisioningCredentials pending_{};
};

}  // namespace ss
