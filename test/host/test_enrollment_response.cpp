// =====================================================================
//  test/host/test_enrollment_response.cpp — L1 unit tests (R-19 Option B)
// ---------------------------------------------------------------------
//  The enrollment-response codec is the device↔backend wire contract for
//  the token→identity exchange. These tests pin the format: required vs.
//  optional fields, multi-line PEM passthrough (the whole reason for the
//  '@@'-marker framing), CR/LF handling, forward-compatible unknowns, and
//  bounded/garbage input. Pure parser — no hardware, no network.
// =====================================================================
#include "test_framework.h"
#include "../../src/net/enrollment_response.h"
#include <string>
#include <cstring>

using namespace ss;

static std::string sv(const EnrollmentField& f) {
    return f.ptr ? std::string(f.ptr, f.len) : std::string();
}

// A realistic multi-line PEM block (the codec must preserve it verbatim).
static const char* kCaPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBkTCB+wIJANll5oH3xN1AMA0GCSqGSIb3DQEBCwUAMBYxFDASBgNVBAMMC1Rl\n"
    "c3QgUm9vdCBDQTAeFw0yMDAxMDEwMDAwMDBaFw0zMDAxMDEwMDAwMDBaMBYxFDAS\n"
    "-----END CERTIFICATE-----";

static std::string buildBody(const std::string& extra = "") {
    std::string b = "@@SSENROLL/1\n";
    b += "@@device_id\nsw-AABBCCDDEEFF\n";
    b += "@@mqtt_host\nbroker.example.com\n";
    b += "@@mqtt_root\nsw\n";
    b += extra;
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";
    b += std::string("@@tls_key\n")  +
         "-----BEGIN EC PRIVATE KEY-----\n"
         "MHcCAQEEIJ+abc123def456ghi789jkl012mno345pqr678stu901vwx234yzA\n"
         "-----END EC PRIVATE KEY-----\n";
    return b;
}

// ---------------- happy path ----------------
TEST(parses_full_valid_response) {
    const std::string b = buildBody();
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(r.valid);
    EQ(sv(r.deviceId), std::string("sw-AABBCCDDEEFF"));
    EQ(sv(r.mqttHost), std::string("broker.example.com"));
    EQ(sv(r.mqttRoot), std::string("sw"));
}

TEST(preserves_multiline_pem_verbatim) {
    const std::string b = buildBody();
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    // The CA PEM must survive intact, internal newlines and all — this is
    // what mbedtls_x509_crt_parse needs (BEGIN/END + base64 lines).
    EQ(sv(r.tlsCa), std::string(kCaPem));
    CHECK(sv(r.tlsCert) == std::string(kCaPem));
    CHECK(sv(r.tlsKey).find("BEGIN EC PRIVATE KEY") != std::string::npos);
    CHECK(sv(r.tlsKey).find("END EC PRIVATE KEY") != std::string::npos);
    // no leading/trailing newline bled into the slice
    CHECK(sv(r.tlsCa).front() == '-');
    CHECK(sv(r.tlsCa).back()  == '-');
}

TEST(optional_fields_may_be_absent) {
    // Drop device_id and mqtt_root entirely — still valid (only the 3 PEMs
    // + mqtt_host are required).
    std::string b = "@@SSENROLL/1\n";
    b += "@@mqtt_host\nb.example.com\n";
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";
    b += std::string("@@tls_key\nKEYBYTES\n");
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(r.valid);
    CHECK(!r.deviceId.present());
    CHECK(!r.mqttRoot.present());
    EQ(sv(r.mqttHost), std::string("b.example.com"));
    EQ(sv(r.tlsKey), std::string("KEYBYTES"));
}

// ---------------- required-field enforcement ----------------
TEST(missing_tls_key_is_invalid) {
    std::string b = "@@mqtt_host\nh\n";
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";   // no tls_key
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(!r.valid);
}

TEST(missing_mqtt_host_is_invalid) {
    std::string b;
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";
    b += std::string("@@tls_key\nK\n");
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(!r.valid);
}

TEST(empty_value_for_required_field_is_invalid) {
    // @@mqtt_host immediately followed by the next marker → empty value.
    std::string b = "@@mqtt_host\n";
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";
    b += std::string("@@tls_key\nK\n");
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(!r.mqttHost.present());
    CHECK(!r.valid);
}

// ---------------- robustness ----------------
TEST(handles_crlf_line_endings) {
    std::string b = "@@SSENROLL/1\r\n@@mqtt_host\r\nbroker\r\n";
    b += std::string("@@tls_ca\r\n")   + kCaPem + "\r\n";
    b += std::string("@@tls_cert\r\n") + kCaPem + "\r\n";
    b += std::string("@@tls_key\r\nK\r\n");
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(r.valid);
    EQ(sv(r.mqttHost), std::string("broker"));   // trailing CR stripped
}

TEST(ignores_unknown_markers_and_version_header) {
    std::string b = "@@SSENROLL/1\n@@future_field\nsomething\n@@mqtt_host\nh\n";
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";
    b += std::string("@@tls_key\nK\n");
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(r.valid);                              // unknown markers don't break it
    EQ(sv(r.mqttHost), std::string("h"));
}

TEST(last_field_without_trailing_newline) {
    std::string b = "@@mqtt_host\nh\n";
    b += std::string("@@tls_ca\n")   + kCaPem + "\n";
    b += std::string("@@tls_cert\n") + kCaPem + "\n";
    b += std::string("@@tls_key\nKEYNOEOL");     // no final newline
    auto r = EnrollmentResponseCodec::parse(b.c_str(), b.size());
    CHECK(r.valid);
    EQ(sv(r.tlsKey), std::string("KEYNOEOL"));
}

TEST(rejects_null_empty_and_oversized) {
    CHECK(!EnrollmentResponseCodec::parse(nullptr, 10).valid);
    CHECK(!EnrollmentResponseCodec::parse("", 0).valid);
    std::string huge(EnrollmentResponseCodec::kMaxResponse + 1, 'x');
    CHECK(!EnrollmentResponseCodec::parse(huge.c_str(), huge.size()).valid);
}

TEST(garbage_without_markers_is_invalid) {
    const char* junk = "not a valid response at all\njust text\n";
    auto r = EnrollmentResponseCodec::parse(junk, strlen(junk));
    CHECK(!r.valid);
}

int main() {
    printf("Enrollment response codec unit tests\n");
    return tf::run_all();
}
