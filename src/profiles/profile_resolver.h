// =====================================================================
//  profiles/profile_resolver.h — ProfileResolver (Platform/Profile)
// ---------------------------------------------------------------------
//  Resolves a ProfileId (selected at runtime from KVS — Q1) to its const
//  DeviceProfile, and derives the OTA HW-compat tag (Phase 3 R-8 / Phase
//  8) used to reject mismatched firmware. Pure lookup over the const
//  registry → host-testable; no platform, no flash here (the KVS read
//  that yields the ProfileId happens in an adapter and is passed in).
//
//  Q1 lifecycle note: the hardware profile is fixed for a given physical
//  device and survives factory reset; only creds are cleared. So an
//  unknown/missing stored ProfileId is a fault to surface, not something
//  to silently "default" — a wrong profile means wrong GPIO. We return a
//  structured error rather than guess a pin map.
// =====================================================================
#pragma once

#include "profile_registry.h"
#include "../domain/result.h"

namespace ss {

// Result of a resolve attempt (query → value, not a command).
struct ProfileLookup {
    const DeviceProfile* profile = nullptr;
    Error error = Error::None;
    bool ok() const { return profile != nullptr && error == Error::None; }
};

class ProfileResolver {
public:
    // Resolve a stored id to its const profile. Unknown id → nullptr +
    // structured error (caller must treat as a fault; never guess pins).
    static ProfileLookup resolve(ProfileId id) {
        for (uint8_t i = 0; i < kProfileCount; ++i) {
            if (kProfiles[i]->id == id)
                return { kProfiles[i], Error::None };
        }
        return { nullptr, Error::InvalidArgument };
    }

    // Resolve from a raw stored byte (as read from KVS). Validates the
    // byte is a known ProfileId before use.
    static ProfileLookup resolveRaw(uint8_t rawId) {
        switch (rawId) {
            case 1: case 2: case 3: case 4:
                return resolve(static_cast<ProfileId>(rawId));
            default:
                return { nullptr, Error::InvalidArgument };
        }
    }

    // OTA HW-compat tag check (Phase 8): does an incoming image's tag
    // match this device's family + channel count? Used to refuse a
    // 4-gang image on a 1-gang device, etc. (R-8).
    static bool hwCompatMatches(const DeviceProfile& p,
                                const char* imageFamily,
                                ChannelId imageChannelCount) {
        if (imageChannelCount != p.channelCount) return false;
        return cstrEqual(imageFamily, p.hwCompatFamily);
    }

private:
    // Tiny constexpr-friendly C-string compare (no <cstring> dependency
    // in the pure layer; keeps this trivially host + device compilable).
    static bool cstrEqual(const char* a, const char* b) {
        if (a == nullptr || b == nullptr) return false;
        while (*a && (*a == *b)) { ++a; ++b; }
        return *a == *b;
    }
};

}  // namespace ss
