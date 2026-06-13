// =====================================================================
//  app/logging_state_listener.h — relay changes → log events (pure)
// ---------------------------------------------------------------------
//  A StateChangePort that records relay transitions to the Logger
//  (Phase 9 control-category events). Registered on the fan-out alongside
//  MQTT, so every relay change is both published AND logged. Pure glue,
//  host-testable; the Logger's TSDB write is the on-device part.
// =====================================================================
#pragma once

#include "../ports/state_change_port.h"
#include "../log/logger.h"

namespace ss {

class LoggingStateListener final : public StateChangePort {
public:
    explicit LoggingStateListener(Logger& logger) : logger_(logger) {}

    void onStateChanged(ChannelId ch, RelayState state, CommandSource src) override {
        // arg1 = channel, arg2 = source (for post-hoc attribution).
        logger_.log(state == RelayState::On ? EventCode::RelayOn : EventCode::RelayOff,
                    Severity::Info, ch, static_cast<uint16_t>(src));
    }

private:
    Logger& logger_;
};

}  // namespace ss
