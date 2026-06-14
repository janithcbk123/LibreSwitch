// =====================================================================
//  net/enrollment_response.h — enrollment response codec (pure, host-tested)
// ---------------------------------------------------------------------
//  Parses the body returned by the R-19 enrollment endpoint into the
//  per-device identity the device must persist. Hand-rolled, bounded,
//  zero-copy (same philosophy as ota_manifest.h / provisioning_types.h —
//  no JSON dependency on this platform).
//
//  WIRE FORMAT (the device↔backend R-19 contract, v1). A small text body
//  of '@@'-prefixed marker lines. Each "@@<field>" line begins a field;
//  its value is every byte until the NEXT line that starts with "@@" (or
//  EOF), with trailing CR/LF trimmed. This is collision-free because PEM
//  never contains a line starting with "@@", so multi-line cert/key blobs
//  pass through verbatim. Unknown markers are ignored (forward-compatible).
//
//    @@SSENROLL/1
//    @@device_id
//    sw-AABBCCDDEEFF
//    @@mqtt_host
//    broker.example.com
//    @@mqtt_root
//    sw
//    @@tls_ca
//    -----BEGIN CERTIFICATE-----
//    ...
//    -----END CERTIFICATE-----
//    @@tls_cert
//    -----BEGIN CERTIFICATE-----
//    ...
//    -----END CERTIFICATE-----
//    @@tls_key
//    -----BEGIN EC PRIVATE KEY-----
//    ...
//    -----END EC PRIVATE KEY-----
//
//  Required: tls_ca, tls_cert, tls_key, mqtt_host (mTLS is mandatory —
//  no identity without all three PEMs + a broker). Optional: device_id,
//  mqtt_root. valid is true only when every required field is present and
//  non-empty. Fields are slices into the caller's buffer (no copying); the
//  on-device writer enforces the KVS size caps before persisting.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ss {

// A zero-copy view into the parsed response buffer (NOT NUL-terminated).
struct EnrollmentField {
    const char* ptr = nullptr;
    size_t      len = 0;
    bool present() const { return ptr != nullptr && len > 0; }
};

struct EnrollmentResponse {
    EnrollmentField deviceId;   // optional
    EnrollmentField mqttHost;   // required
    EnrollmentField mqttRoot;   // optional (composition root defaults "sw")
    EnrollmentField tlsCa;      // required (PEM)
    EnrollmentField tlsCert;    // required (PEM)
    EnrollmentField tlsKey;     // required (PEM)
    bool valid = false;
};

class EnrollmentResponseCodec {
public:
    // Upper bound on the whole response (3 PEMs ~1.8 KB each + scalars +
    // framing). Anything larger is rejected outright — never trust length.
    static constexpr size_t kMaxResponse = 8192;

    static EnrollmentResponse parse(const char* body, size_t len) {
        EnrollmentResponse r{};
        if (!body || len == 0 || len > kMaxResponse) return r;

        EnrollmentField* cur = nullptr;   // field whose value we're accruing
        size_t i = 0;
        while (i <= len) {
            // line = [i, eol); eol points at the '\n' or at len (last line)
            size_t eol = i;
            while (eol < len && body[eol] != '\n') ++eol;

            const bool marker = (eol - i >= 2) && body[i] == '@' && body[i + 1] == '@';
            if (marker) {
                // close the previous field at the start of this marker line
                if (cur) trimEnd(*cur, body + i);
                // field name = rest of marker line (sans leading "@@", trailing CR)
                size_t ns = i + 2, ne = eol;
                if (ne > ns && body[ne - 1] == '\r') --ne;
                cur = fieldFor(r, body + ns, ne - ns);
                if (cur) { cur->ptr = (eol < len) ? body + eol + 1 : body + len; cur->len = 0; }
            }
            if (eol >= len) break;        // consumed final line
            i = eol + 1;
        }
        if (cur) trimEnd(*cur, body + len);   // close the last field at EOF

        r.valid = r.tlsCa.present() && r.tlsCert.present() &&
                  r.tlsKey.present() && r.mqttHost.present();
        return r;
    }

private:
    // Set field length from its ptr up to `end`, trimming trailing CR/LF.
    static void trimEnd(EnrollmentField& f, const char* end) {
        if (!f.ptr || end < f.ptr) { f.len = 0; return; }
        size_t n = static_cast<size_t>(end - f.ptr);
        while (n > 0 && (f.ptr[n - 1] == '\n' || f.ptr[n - 1] == '\r')) --n;
        f.len = n;
    }

    static bool name_is(const char* k, size_t klen, const char* lit) {
        return klen == strlen(lit) && memcmp(k, lit, klen) == 0;
    }

    // Map a marker name to its slot; unknown names → nullptr (ignored).
    static EnrollmentField* fieldFor(EnrollmentResponse& r, const char* k, size_t klen) {
        if (name_is(k, klen, "device_id")) return &r.deviceId;
        if (name_is(k, klen, "mqtt_host")) return &r.mqttHost;
        if (name_is(k, klen, "mqtt_root")) return &r.mqttRoot;
        if (name_is(k, klen, "tls_ca"))    return &r.tlsCa;
        if (name_is(k, klen, "tls_cert"))  return &r.tlsCert;
        if (name_is(k, klen, "tls_key"))   return &r.tlsKey;
        return nullptr;   // version header (@@SSENROLL/1) + unknowns
    }
};

}  // namespace ss
