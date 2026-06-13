// =====================================================================
//  test/host/test_provisioning.cpp — L1 unit tests (Phase 7)
// ---------------------------------------------------------------------
//  Form parsing/URL-decode, the sequential credential-test ordering
//  (AP down BEFORE STA, restored on failure), creds-persisted-only-on-
//  success, enrollment w/ one-time token, already-provisioned fast path,
//  and the AP idle timeout (R-20).
// =====================================================================
#include "test_framework.h"
#include "../../src/net/provisioning_service.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>

using namespace ss;

// ---------------- form parsing ----------------
TEST(form_field_extracts_value) {
    char out[64];
    CHECK(ProvForm::field("ssid=MyNet&pass=secret", "ssid", out, sizeof(out)));
    EQ(std::string(out), std::string("MyNet"));
    CHECK(ProvForm::field("ssid=MyNet&pass=secret", "pass", out, sizeof(out)));
    EQ(std::string(out), std::string("secret"));
}
TEST(form_field_absent_returns_false) {
    char out[64];
    CHECK(!ProvForm::field("ssid=X", "token", out, sizeof(out)));
    EQ(std::string(out), std::string(""));
}
TEST(form_url_decode_percent_and_plus) {
    char out[64];
    ProvForm::urlDecode("My%20Net+2", 10, out, sizeof(out));
    EQ(std::string(out), std::string("My Net 2"));
}
TEST(form_parse_requires_ssid) {
    auto a = ProvForm::parse("pass=x&token=y");      // no ssid
    CHECK(!a.valid);
    auto b = ProvForm::parse("ssid=Home&pass=p&token=T123");
    CHECK(b.valid);
    EQ(std::string(b.ssid), std::string("Home"));
    EQ(std::string(b.token), std::string("T123"));
}
TEST(form_parse_decodes_special_chars) {
    auto c = ProvForm::parse("ssid=Caf%C3%A9&pass=p%40ss%20word");
    EQ(std::string(c.ssid), std::string("Caf\xC3\xA9"));   // UTF-8 café
    EQ(std::string(c.pass), std::string("p@ss word"));
}

// ---------------- mocks ----------------
struct MockWifi : WifiControl {
    std::vector<std::string> events;   // ordered trace of operations
    bool apUp = false, staUp = false;
    bool nextStaResult = true;
    bool startAp(const char* s, const char*) override { apUp = true; events.push_back(std::string("startAp:") + s); return true; }
    void stopAp() override { apUp = false; events.push_back("stopAp"); }
    bool connectSta(const char* s, const char*, uint32_t) override {
        events.push_back(std::string("connectSta:") + s);
        staUp = nextStaResult; return staUp;
    }
    bool isStaConnected() override { return staUp; }
    void disconnectSta() override { staUp = false; events.push_back("disconnectSta"); }
};
struct MockPortal : PortalServer {
    bool running = false; std::vector<std::string> messages;
    PortalSubmissionHandler* handler = nullptr;
    void start(PortalSubmissionHandler& h) override { running = true; handler = &h; }
    void stop() override { running = false; }
    void poll() override {}
    void setMessage(const char* m) override { messages.push_back(m); }
};
struct MockEnroll : Enrollment {
    bool hasCert = false; bool nextResult = true; std::string lastToken = "<none>";
    bool enroll(const char* token) override { lastToken = token ? token : "<null>"; hasCert = nextResult; return nextResult; }
    bool hasOperationalCert() override { return hasCert; }
};
struct MockKv : KvStore {
    std::map<std::string, std::vector<uint8_t>> store;
    bool erase(const char* k) override { store.erase(k); return true; }

    bool setBlob(const char* k, const void* d, size_t n) override {
        const uint8_t* p = (const uint8_t*)d; store[k] = std::vector<uint8_t>(p, p+n); return true;
    }
    size_t getBlob(const char* k, void* buf, size_t cap) override {
        auto it = store.find(k); if (it == store.end()) return 0;
        size_t n = std::min(cap, it->second.size()); memcpy(buf, it->second.data(), n);
        return it->second.size();
    }
};

static ProvConfig cfg() { return ProvConfig{"Switch-A1B2", "pass1234", 20000, 600000}; }

struct Rig {
    MockWifi wifi; MockPortal portal; MockEnroll enroll; MockKv kv;
    ProvisioningService svc{wifi, portal, enroll, kv, cfg()};
};

// ---------------- state machine ----------------
TEST(begin_opens_portal_when_unprovisioned) {
    Rig r;
    EQ(r.svc.begin(0), ProvState::ApPortal);
    CHECK(r.wifi.apUp);
    CHECK(r.portal.running);
}

TEST(begin_goes_operational_when_already_provisioned) {
    Rig r;
    r.kv.setBlob("sta_ssid", "Home", 5);
    r.kv.setBlob("sta_pass", "pw", 3);
    r.enroll.hasCert = true;                 // already enrolled
    r.wifi.nextStaResult = true;
    EQ(r.svc.begin(0), ProvState::Operational);
    CHECK(!r.portal.running);                // no portal needed
}

// THE critical invariant: AP must be torn down BEFORE the STA attempt.
TEST(sequential_test_drops_ap_before_sta) {
    Rig r;
    r.svc.begin(0);
    r.wifi.events.clear();
    r.wifi.nextStaResult = true;
    r.svc.onCredentialsSubmitted(ProvForm::parse("ssid=Home&pass=pw&token=T1"));
    // trace must show stopAp strictly before connectSta
    int iStop = -1, iSta = -1;
    for (size_t i = 0; i < r.wifi.events.size(); ++i) {
        if (r.wifi.events[i] == "stopAp") iStop = (int)i;
        if (r.wifi.events[i].rfind("connectSta:", 0) == 0) iSta = (int)i;
    }
    CHECK(iStop >= 0); CHECK(iSta >= 0);
    CHECK(iStop < iSta);                      // AP down BEFORE STA (D7-3)
}

TEST(successful_test_persists_creds_and_enrolls) {
    Rig r; r.svc.begin(0);
    r.wifi.nextStaResult = true; r.enroll.nextResult = true;
    r.svc.onCredentialsSubmitted(ProvForm::parse("ssid=Home&pass=pw&token=TOKEN9"));
    EQ(r.svc.state(), ProvState::Operational);
    // creds persisted
    char ssid[kMaxSsid]{}; CHECK(r.kv.getBlob("sta_ssid", ssid, sizeof(ssid)) > 0);
    EQ(std::string(ssid), std::string("Home"));
    // enrollment got the one-time token (R-19)
    EQ(r.enroll.lastToken, std::string("TOKEN9"));
}

TEST(failed_sta_does_not_persist_and_restores_ap) {
    Rig r; r.svc.begin(0);
    r.wifi.nextStaResult = false;            // STA join fails
    r.svc.onCredentialsSubmitted(ProvForm::parse("ssid=Bad&pass=wrong"));
    // failure restores the portal (resting state = ApPortal, not a stuck
    // Failed state — failure is a transition back to the portal for retry)
    EQ(r.svc.state(), ProvState::ApPortal);
    // creds NOT persisted (don't brick into silent non-connecting state)
    char ssid[kMaxSsid]{}; EQ(r.kv.getBlob("sta_ssid", ssid, sizeof(ssid)), (size_t)0);
    // AP restored for retry + user told why
    CHECK(r.wifi.apUp);
    CHECK(r.portal.running);
    bool sawRetryMsg = false;
    for (auto& m : r.portal.messages) if (m.find("retry") != std::string::npos || m.find("connect") != std::string::npos) sawRetryMsg = true;
    CHECK(sawRetryMsg);
}

TEST(failed_enrollment_does_not_go_operational) {
    Rig r; r.svc.begin(0);
    r.wifi.nextStaResult = true;
    r.enroll.nextResult = false;             // enrollment fails (bad token)
    r.svc.onCredentialsSubmitted(ProvForm::parse("ssid=Home&pass=pw&token=BAD"));
    CHECK(r.svc.state() != ProvState::Operational);
    CHECK(r.wifi.apUp);                       // back to portal
}

TEST(invalid_submission_keeps_portal) {
    Rig r; r.svc.begin(0);
    r.svc.onCredentialsSubmitted(ProvForm::parse("pass=only"));   // no ssid
    EQ(r.svc.state(), ProvState::ApPortal);
    CHECK(!r.portal.messages.empty());        // user told it's invalid
}

TEST(ap_idle_timeout_flags_after_window) {
    Rig r; r.svc.begin(0);
    r.svc.tick(100);            CHECK(!r.svc.idleTimedOut());
    r.svc.tick(600000);         CHECK(r.svc.idleTimedOut());   // R-20 window
}

TEST(no_token_passes_null_to_enroll) {
    Rig r; r.svc.begin(0);
    r.wifi.nextStaResult = true;
    r.svc.onCredentialsSubmitted(ProvForm::parse("ssid=Home&pass=pw"));  // no token
    EQ(r.enroll.lastToken, std::string("<null>"));
}

int main() {
    printf("Provisioning unit tests\n");
    return tf::run_all();
}
