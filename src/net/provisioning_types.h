// =====================================================================
//  net/provisioning_types.h — provisioning value types + form parse (pure)
// ---------------------------------------------------------------------
//  Phase 7. The provisioning state machine + the captive-portal form
//  parsing are pure and host-testable. WiFi/WebServer I/O lives behind
//  seams. Vendor-neutral, no hardcoded prefixes.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ss {

// Phase 7 state machine (D7-1). Local control is live in EVERY state —
// provisioning governs connectivity only, never the control path.
enum class ProvState : uint8_t {
    ApPortal         = 0,  // SoftAP up, serving the config form
    TestingCreds     = 1,  // sequential: AP down, trying STA join
    IdentityBootstrap= 2,  // STA ok, doing keygen→CSR→cert (enrollment)
    Operational      = 3,  // connected + identified; normal run
    Failed           = 4,  // creds test failed → will return to ApPortal
};

// Bounds sized for real SSID/passphrase/token. SSID max 32 (802.11),
// WPA passphrase max 63, token bounded.
inline constexpr size_t kMaxSsid  = 33;   // +NUL
inline constexpr size_t kMaxPass  = 64;   // +NUL
inline constexpr size_t kMaxToken = 65;   // +NUL

struct ProvisioningCredentials {
    char ssid[kMaxSsid]   = {};
    char pass[kMaxPass]   = {};
    char token[kMaxToken] = {};   // one-time enrollment token (R-19)
    bool valid = false;           // set true only after a clean parse

    bool hasSsid() const { return ssid[0] != '\0'; }
    bool hasToken() const { return token[0] != '\0'; }
};

// Pure helpers for the captive-portal form. The on-device WebServer can
// use WebServer.arg() directly, but these let us parse a raw URL-encoded
// body and are fully host-tested (used as the canonical parse).
class ProvForm {
public:
    // URL-decode one field value in place into `out` (handles %XX and '+').
    // Returns length written (excl NUL); truncates safely to outCap-1.
    static size_t urlDecode(const char* in, size_t inLen, char* out, size_t outCap) {
        size_t o = 0;
        for (size_t i = 0; i < inLen && o + 1 < outCap; ++i) {
            char c = in[i];
            if (c == '+') { out[o++] = ' '; }
            else if (c == '%' && i + 2 < inLen && isHex(in[i+1]) && isHex(in[i+2])) {
                out[o++] = static_cast<char>((hexVal(in[i+1]) << 4) | hexVal(in[i+2]));
                i += 2;
            } else { out[o++] = c; }
        }
        out[o] = '\0';
        return o;
    }

    // Extract a field named `key` from an "a=b&c=d" URL-encoded body into
    // `out` (URL-decoded). Returns true if the key was present.
    static bool field(const char* body, const char* key, char* out, size_t outCap) {
        const size_t klen = strlen(key);
        const char* p = body;
        while (p && *p) {
            const char* eq = strchr(p, '=');
            if (!eq) break;
            const size_t namelen = static_cast<size_t>(eq - p);
            const char* val = eq + 1;
            const char* amp = strchr(val, '&');
            const size_t vallen = amp ? static_cast<size_t>(amp - val) : strlen(val);
            if (namelen == klen && strncmp(p, key, klen) == 0) {
                urlDecode(val, vallen, out, outCap);
                return true;
            }
            p = amp ? amp + 1 : nullptr;
        }
        if (outCap) out[0] = '\0';
        return false;
    }

    // Parse a full submission body into credentials. SSID is required;
    // pass + token optional (open networks; token may be pre-shared).
    static ProvisioningCredentials parse(const char* body) {
        ProvisioningCredentials c{};
        field(body, "ssid",  c.ssid,  sizeof(c.ssid));
        field(body, "pass",  c.pass,  sizeof(c.pass));
        field(body, "token", c.token, sizeof(c.token));
        c.valid = c.hasSsid();             // SSID mandatory
        return c;
    }

private:
    static bool isHex(char c) {
        return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
    }
    static uint8_t hexVal(char c) {
        if (c>='0'&&c<='9') return c-'0';
        if (c>='a'&&c<='f') return c-'a'+10;
        return c-'A'+10;
    }
};

}  // namespace ss
