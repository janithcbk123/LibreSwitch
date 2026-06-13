// =====================================================================
//  net/mqtt_payload.h — MQTT payload codec (pure, host-testable)
// ---------------------------------------------------------------------
//  Phase 6: compact JSON payloads. Deliberately NOT a general JSON
//  library (ArduinoJson is RAM-hungry and would fight the ~19 KB TLS
//  floor). The message shapes are fixed and tiny, so we hand-roll a
//  minimal, DEFENSIVE codec for exactly the known shapes:
//    inbound  cmd:  {"on":true}  / {"on":false}
//    outbound evt:  {"on":true,"src":"local","ts":12345}
//
//  Defensive parse: anything not matching the expected shape → rejected
//  (returns false), never a misparse. Serialize writes into a caller
//  buffer (no heap). Host-testable in full.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "../domain/relay_types.h"

namespace ss {

class MqttPayload {
public:
    // Parse a relay command payload. Accepts {"on":true} / {"on":false}
    // with arbitrary surrounding whitespace. Sets `on` and returns true
    // only on a clean match; otherwise false (caller ignores the msg).
    static bool parseRelaySet(const uint8_t* data, size_t len, bool& on) {
        if (!data || len == 0 || len > 64) return false;     // bound it
        char buf[65];
        memcpy(buf, data, len);
        buf[len] = '\0';
        const char* key = strstr(buf, "\"on\"");
        if (!key) return false;
        const char* colon = strchr(key, ':');
        if (!colon) return false;
        const char* v = skipSpace(colon + 1);
        if (strncmp(v, "true", 4) == 0)  { on = true;  return true; }
        if (strncmp(v, "false", 5) == 0) { on = false; return true; }
        return false;                                        // malformed
    }

    // Serialize a relay state event: {"on":<bool>,"src":"<src>","ts":<n>}.
    // Returns bytes written (excluding NUL), or 0 if it didn't fit.
    static size_t relayState(char* out, size_t n,
                             RelayState state, CommandSource src, uint32_t ts) {
        const int w = snprintf(out, n, "{\"on\":%s,\"src\":\"%s\",\"ts\":%lu}",
                               state == RelayState::On ? "true" : "false",
                               sourceName(src),
                               static_cast<unsigned long>(ts));
        return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
    }

    // Online/LWT payloads.
    static size_t online(char* out, size_t n, bool isOnline) {
        const int w = snprintf(out, n, "{\"online\":%s}", isOnline ? "true" : "false");
        return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
    }

private:
    static const char* skipSpace(const char* p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
        return p;
    }
    static const char* sourceName(CommandSource s) {
        switch (s) {
            case CommandSource::Local:    return "local";
            case CommandSource::Cloud:    return "cloud";
            case CommandSource::Lan:      return "lan";
            case CommandSource::Boot:     return "boot";
            case CommandSource::Internal: return "internal";
        }
        return "internal";
    }
};

}  // namespace ss
