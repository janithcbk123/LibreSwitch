// =====================================================================
//  net/mqtt_topics.h — MQTT topic builder (pure, host-testable)
// ---------------------------------------------------------------------
//  Phase 6 D6-2: capability-based, vendor-neutral topics under a
//  config-driven root: <root>/<device_id>/...  The root and device_id
//  come from config (KVS) — no vendor strings, no hardcoded prefix.
//
//  Pure string composition into caller-provided buffers (no heap, no
//  std::string on-device). Fixed bounded buffers sized for the longest
//  topic. Host-testable in full.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "../domain/relay_types.h"

namespace ss {

// Max topic length we build. Longest: "<root>/<id>/evt/relay/<ch>/state".
// root<=24, id<=32, suffix<=24 → 80 leaves margin. Bounded, no heap.
inline constexpr size_t kMaxTopicLen = 96;

class MqttTopics {
public:
    // root and deviceId are borrowed (must outlive this object) — they
    // live in the config store. Neither may be null.
    MqttTopics(const char* root, const char* deviceId)
        : root_(root ? root : "sw"), id_(deviceId ? deviceId : "unknown") {}

    // ---- downlink (subscribe) ----
    // All commands arrive under <root>/<id>/cmd/#  → one wildcard sub.
    bool cmdWildcard(char* out, size_t n) const { return fmt(out, n, "cmd/#"); }

    bool cmdRelaySet(char* out, size_t n, ChannelId ch) const {
        char suf[24]; snprintf(suf, sizeof(suf), "cmd/relay/%u/set", (unsigned)ch);
        return fmt(out, n, suf);
    }
    bool cmdRelayAllSet(char* out, size_t n) const { return fmt(out, n, "cmd/relay/all/set"); }

    // System commands (Phase 6/8): OTA manifest + post-OTA health confirm.
    // Suffix-exact match (endsWith) so "ota" never collides with other
    // names; both arrive via the single cmd/# wildcard subscription.
    static bool isSystemOta(const char* topic)     { return endsWith(topic, "cmd/system/ota"); }
    static bool isSystemConfirm(const char* topic) { return endsWith(topic, "cmd/system/confirm"); }

    // ---- uplink (publish) ----
    bool evtRelayState(char* out, size_t n, ChannelId ch) const {
        char suf[24]; snprintf(suf, sizeof(suf), "evt/relay/%u/state", (unsigned)ch);
        return fmt(out, n, suf);
    }
    bool evtOnline(char* out, size_t n) const { return fmt(out, n, "evt/system/online"); }
    bool evtHealth(char* out, size_t n) const { return fmt(out, n, "evt/system/health"); }

    static bool endsWith(const char* s, const char* suf) {
        const size_t ls = strlen(s), lf = strlen(suf);
        return ls >= lf && memcmp(s + ls - lf, suf, lf) == 0;
    }

    // Parse the channel out of an incoming "cmd/relay/<ch>/set" topic.
    // Returns true + sets ch on match; false otherwise. Used by routing.
    // `topic` is the FULL topic; we locate the "/cmd/relay/" segment.
    static bool parseRelaySetChannel(const char* topic, ChannelId& ch) {
        const char* p = strstr(topic, "cmd/relay/");
        if (!p) return false;
        p += strlen("cmd/relay/");
        if (*p < '0' || *p > '9') return false;       // "all" or malformed
        unsigned v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        if (strncmp(p, "/set", 4) != 0) return false;
        ch = static_cast<ChannelId>(v);
        return true;
    }

    static bool isRelayAllSet(const char* topic) {
        return strstr(topic, "cmd/relay/all/set") != nullptr;
    }

private:
    bool fmt(char* out, size_t n, const char* suffix) const {
        const int w = snprintf(out, n, "%s/%s/%s", root_, id_, suffix);
        return w > 0 && static_cast<size_t>(w) < n;   // false if truncated
    }

    const char* root_;
    const char* id_;
};

}  // namespace ss
