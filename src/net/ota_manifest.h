// =====================================================================
//  net/ota_manifest.h — OTA manifest codec (pure, host-tested)
// ---------------------------------------------------------------------
//  Parses the cmd/system/ota payload into an OtaManifest. Hand-rolled
//  key=value codec (no JSON dependency — same philosophy as
//  mqtt_payload.h). Bounded copies only; never trusts lengths.
//
//  Wire format (single line, ';'-separated; '&' and '\n' also accepted):
//    url=https://host/fw.uf2;ver=1.2.3;sig=<base64>;hw=bk7231n.relay_touch;ch=4;size=123456;dg=0
//
//  Required: url, ver, sig, hw, ch, size.  Optional: dg (downgrade, "1"
//  enables the signed escape hatch — D8-3).  manifest.valid is true only
//  if all required fields parsed sane.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "ota_types.h"

namespace ss {

class OtaManifestCodec {
public:
    static OtaManifest parse(const uint8_t* payload, size_t len) {
        OtaManifest m{};
        if (!payload || len == 0 || len > 1024) return m;   // bounded input

        bool haveUrl=false, haveVer=false, haveSig=false,
             haveHw=false, haveCh=false, haveSize=false;

        size_t i = 0;
        while (i < len) {
            // token = [i, j) up to separator
            size_t j = i;
            while (j < len && payload[j] != ';' && payload[j] != '&' &&
                   payload[j] != '\n' && payload[j] != '\r') ++j;
            // split token at '='
            size_t eq = i;
            while (eq < j && payload[eq] != '=') ++eq;
            if (eq > i && eq < j) {
                const char* k = reinterpret_cast<const char*>(payload + i);
                const size_t klen = eq - i;
                const char* v = reinterpret_cast<const char*>(payload + eq + 1);
                const size_t vlen = j - eq - 1;
                if (vlen > 0) {
                    if (match(k, klen, "url"))  haveUrl  = copyTo(m.url,  sizeof(m.url),  v, vlen);
                    else if (match(k, klen, "sig"))  haveSig  = copyTo(m.sigB64, sizeof(m.sigB64), v, vlen);
                    else if (match(k, klen, "hw"))   haveHw   = copyTo(m.hwFamily, sizeof(m.hwFamily), v, vlen);
                    else if (match(k, klen, "ver"))  haveVer  = parseVer(m.version, v, vlen);
                    else if (match(k, klen, "ch"))   haveCh   = parseU8(m.hwChannels, v, vlen);
                    else if (match(k, klen, "size")) haveSize = parseU32(m.sizeBytes, v, vlen);
                    else if (match(k, klen, "dg"))   m.allowDowngrade = (v[0] == '1');
                    // unknown keys ignored (forward-compatible)
                }
            }
            i = j + 1;
        }

        m.valid = haveUrl && haveVer && haveSig && haveHw && haveCh && haveSize
                  && m.hwChannels >= 1 && m.hwChannels <= kMaxChannels;
        return m;
    }

private:
    static bool match(const char* k, size_t klen, const char* name) {
        return klen == strlen(name) && memcmp(k, name, klen) == 0;
    }
    // Bounded copy; rejects values that don't fit (truncation = invalid).
    static bool copyTo(char* dst, size_t cap, const char* v, size_t vlen) {
        if (vlen + 1 > cap) return false;
        memcpy(dst, v, vlen); dst[vlen] = '\0'; return true;
    }
    static bool parseU32(uint32_t& out, const char* v, size_t vlen) {
        if (vlen == 0 || vlen > 10) return false;
        uint32_t r = 0;
        for (size_t i = 0; i < vlen; ++i) {
            if (v[i] < '0' || v[i] > '9') return false;
            r = r * 10u + (uint32_t)(v[i] - '0');
        }
        out = r; return true;
    }
    static bool parseU8(ChannelId& out, const char* v, size_t vlen) {
        uint32_t r; if (!parseU32(r, v, vlen) || r > 255) return false;
        out = (ChannelId)r; return true;
    }
    // "major.minor.patch", each numeric.
    static bool parseVer(FwVersion& out, const char* v, size_t vlen) {
        uint32_t parts[3] = {0,0,0}; int pi = 0; uint32_t cur = 0; bool any=false;
        for (size_t i = 0; i < vlen; ++i) {
            if (v[i] == '.') {
                if (!any || pi >= 2) return false;
                parts[pi++] = cur; cur = 0; any = false;
            } else if (v[i] >= '0' && v[i] <= '9') {
                cur = cur * 10u + (uint32_t)(v[i] - '0'); any = true;
                if (cur > 65535) return false;
            } else return false;
        }
        if (!any || pi != 2) return false;
        parts[2] = cur;
        out.major = (uint16_t)parts[0]; out.minor = (uint16_t)parts[1];
        out.patch = (uint16_t)parts[2];
        return true;
    }
};

}  // namespace ss
