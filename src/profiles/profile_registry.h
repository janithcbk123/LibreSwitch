// =====================================================================
//  profiles/profile_registry.h — const profile registry (flash-resident)
// ---------------------------------------------------------------------
//  The concrete 1–4 gang profiles. All `constexpr` → resident in flash,
//  zero RAM cost (Phase 2: profiles const in flash; Q1: hardware profile
//  survives factory reset). Reference HW pin map (from project context):
//    relays  GPIO 6, 8, 9, 26   touch GPIO 24, 20, 7, 14
//    status LED GPIO22           backlight GPIO23
//  Lower-gang variants are capability subsets of the 4-gang map.
//
//  Naming is capability-based and vendor-neutral (Phase 1 AO-1): the
//  profile names are "switch_1g".."switch_4g"; the OTA HW-compat family
//  is "bk7231n.relay_touch" + channel count — no vendor identifiers.
// =====================================================================
#pragma once

#include "device_profile.h"

namespace ss {

inline constexpr const char* kHwFamily = "bk7231n.relay_touch";

// ---------------------------------------------------------------------
//  PIN MAP — overridable at build time without editing this file.
//  Defaults are the confirmed reference pins (relays 6/8/9/26, touch
//  24/20/7/14, LED 22, backlight 23). To retarget a different board,
//  pass e.g.  -D RELAY_PIN_0=10  -D TOUCH_PIN_0=11  in platformio.ini
//  build_flags. Anything not overridden keeps the default below.
// ---------------------------------------------------------------------
#ifndef RELAY_PIN_0
#define RELAY_PIN_0 6
#endif
#ifndef RELAY_PIN_1
#define RELAY_PIN_1 8
#endif
#ifndef RELAY_PIN_2
#define RELAY_PIN_2 9
#endif
#ifndef RELAY_PIN_3
#define RELAY_PIN_3 26
#endif
#ifndef TOUCH_PIN_0
#define TOUCH_PIN_0 24
#endif
#ifndef TOUCH_PIN_1
#define TOUCH_PIN_1 20
#endif
#ifndef TOUCH_PIN_2
#define TOUCH_PIN_2 7
#endif
#ifndef TOUCH_PIN_3
#define TOUCH_PIN_3 14
#endif
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 22
#endif
#ifndef BACKLIGHT_PIN
#define BACKLIGHT_PIN 23
#endif

// Reference per-channel pin assignments (index 0..3).
inline constexpr ChannelMap kRefCh0 = { /*relay*/ RELAY_PIN_0, /*touch*/ TOUCH_PIN_0 };
inline constexpr ChannelMap kRefCh1 = { /*relay*/ RELAY_PIN_1, /*touch*/ TOUCH_PIN_1 };
inline constexpr ChannelMap kRefCh2 = { /*relay*/ RELAY_PIN_2, /*touch*/ TOUCH_PIN_2 };
inline constexpr ChannelMap kRefCh3 = { /*relay*/ RELAY_PIN_3, /*touch*/ TOUCH_PIN_3 };

inline constexpr uint8_t kStatusLedPin = STATUS_LED_PIN;
inline constexpr uint8_t kBacklightPin = BACKLIGHT_PIN;

// --- 4-gang ---
inline constexpr DeviceProfile kSwitch4G = {
    ProfileId::Switch4G, "switch_4g", 4,
    { kRefCh0, kRefCh1, kRefCh2, kRefCh3 },
    kStatusLedPin, kBacklightPin, /*pwmCapable*/ false,
    /*resetPairValid*/ true, /*A*/ 0, /*B*/ 1,
    kHwFamily,
};

// --- 3-gang ---
inline constexpr DeviceProfile kSwitch3G = {
    ProfileId::Switch3G, "switch_3g", 3,
    { kRefCh0, kRefCh1, kRefCh2, {} },
    kStatusLedPin, kBacklightPin, false,
    true, 0, 1,
    kHwFamily,
};

// --- 2-gang ---
inline constexpr DeviceProfile kSwitch2G = {
    ProfileId::Switch2G, "switch_2g", 2,
    { kRefCh0, kRefCh1, {}, {} },
    kStatusLedPin, kBacklightPin, false,
    true, 0, 1,
    kHwFamily,
};

// --- 1-gang: no valid reset pair → alternate gesture (Q4) ---
inline constexpr DeviceProfile kSwitch1G = {
    ProfileId::Switch1G, "switch_1g", 1,
    { kRefCh0, {}, {}, {} },
    kStatusLedPin, kBacklightPin, false,
    /*resetPairValid*/ false, 0, 0,
    kHwFamily,
};

// Registry, indexable for the resolver. Order is not significant.
inline constexpr const DeviceProfile* kProfiles[] = {
    &kSwitch1G, &kSwitch2G, &kSwitch3G, &kSwitch4G,
};
inline constexpr uint8_t kProfileCount =
    sizeof(kProfiles) / sizeof(kProfiles[0]);

}  // namespace ss
