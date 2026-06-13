// =====================================================================
//  app/serial_state_listener.h — print relay changes to UART (on-device)
// ---------------------------------------------------------------------
//  A StateChangePort that prints every relay transition to Serial1, so
//  you can watch state changes on the bench:
//      [state] ch=2 ON  src=local
//  Registered on the fan-out alongside MQTT / logging / persistence.
//
//  On-device only (uses Serial1); compiled out of the host tests.
// =====================================================================
#pragma once

#include "../ports/state_change_port.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <Arduino.h>

namespace ss {

class SerialStateListener final : public StateChangePort {
public:
    void onStateChanged(ChannelId ch, RelayState state, CommandSource src) override {
        Serial1.print("[state] ch=");
        Serial1.print(static_cast<int>(ch));
        Serial1.print(state == RelayState::On ? " ON " : " OFF");
        Serial1.print(" src=");
        Serial1.println(sourceName(src));
    }

private:
    static const char* sourceName(CommandSource s) {
        switch (s) {
            case CommandSource::Local:    return "local";
            case CommandSource::Cloud:    return "cloud";
            case CommandSource::Lan:      return "lan";
            case CommandSource::Boot:     return "boot";
            case CommandSource::Internal: return "internal";
            default:                      return "other";
        }
    }
};

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
