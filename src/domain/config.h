// =====================================================================
//  domain/config.h — shared Core Domain compile-time constants
// ---------------------------------------------------------------------
//  Constants used by more than one domain component live here so no
//  single component "owns" them (DRY; avoids fragile transitive
//  includes). Pure constants only — no logic, no platform.
// =====================================================================
#pragma once

#include "relay_types.h"   // ChannelId

namespace ss {

// Compile-time upper bound on channels; the active count comes from the
// device profile at runtime (Phase 1 AO-1: 1–4 gang via profile).
inline constexpr ChannelId kMaxChannels = 4;

}  // namespace ss
