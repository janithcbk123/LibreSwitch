// =====================================================================
//  net/net_time.h — start SNTP once Wi-Fi is up (ON-DEVICE)
// ---------------------------------------------------------------------
//  The Phase 9 dual-clock logger reads UTC from time(nullptr); that only
//  works after SNTP sets the system clock. VERIFIED against the platform:
//  lwIP's sntp app is compiled in, and the platform's lwipopts.h wires
//  SNTP_SET_SYSTEM_TIME_US → settimeofday (cores/common/base/config/
//  lwipopts.h), so sntp_init() is all that's needed. SNTP_SERVER_DNS=1
//  lets us use a hostname.
//
//  Idempotent: safe to call on every Operational transition.
// =====================================================================
#pragma once

#if defined(LT_BUILD) || defined(ARDUINO)

#include <lwip/apps/sntp.h>

namespace ss {

inline void startSntpOnce(const char* server = "pool.ntp.org") {
    static bool started = false;
    if (started) return;
    started = true;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, server);
    sntp_init();
}

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
