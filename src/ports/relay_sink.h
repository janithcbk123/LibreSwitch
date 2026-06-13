// =====================================================================
//  ports/relay_sink.h — outbound port (Core Domain → platform)
// ---------------------------------------------------------------------
//  The RelayController commands physical relays THROUGH this port; a
//  platform adapter (LibreTiny GPIO) implements it. Phase 2 dependency
//  rule: the domain depends only on this abstract interface, never on
//  LibreTiny. This is also the seam that makes RelayController
//  host-unit-testable with a mock (Phase 10 L1).
// =====================================================================
#pragma once

#include "../domain/relay_types.h"

namespace ss {

// Pure virtual sink. Synchronous, must be fast (<50 ms control budget,
// Phase 1). Implementations must be lock-free on the Control task path
// (Phase 3: Control task owns ALL relay writes).
class RelaySink {
public:
    virtual ~RelaySink() = default;

    // Drive one physical channel to the given state. Returns false only
    // on a hard hardware-layer failure; the domain treats the channel
    // set as authoritative and logs failures (Phase 9 fault code).
    virtual bool driveRelay(ChannelId ch, RelayState state) = 0;
};

}  // namespace ss
