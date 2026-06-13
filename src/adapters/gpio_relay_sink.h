// =====================================================================
//  adapters/gpio_relay_sink.h — GpioRelaySink (outbound adapter)
// ---------------------------------------------------------------------
//  Implements the domain's RelaySink by driving physical GPIO through
//  the GpioHal seam, mapping logical ChannelId → physical pin via the
//  active DeviceProfile. Handles relay polarity (active-high vs the very
//  common active-LOW relay boards) — getting this wrong inverts every
//  relay, so it is explicit, not assumed.
//
//  All logic here (mapping, polarity, init-to-safe-OFF) is host-testable
//  with a mock GpioHal; only the LibreTiny GpioHal impl needs the device.
//  Clean Code: small functions, single responsibility (pin I/O only —
//  no state ownership; the domain's RelayController owns logical state).
// =====================================================================
#pragma once

#include "../ports/relay_sink.h"
#include "../platform/gpio_hal.h"
#include "../profiles/device_profile.h"
#include "../domain/result.h"

namespace ss {

// Relay drive polarity. Active-LOW is common on opto-isolated relay
// boards (pin LOW = relay energized). Default explicit, not guessed.
enum class RelayPolarity : uint8_t {
    ActiveHigh = 0,   // pin HIGH = relay ON
    ActiveLow  = 1,   // pin LOW  = relay ON
};

class GpioRelaySink final : public RelaySink {
public:
    GpioRelaySink(GpioHal& hal, const DeviceProfile& profile,
                  RelayPolarity polarity = RelayPolarity::ActiveHigh)
        : hal_(hal), profile_(profile), polarity_(polarity) {}

    // Configure each present channel's relay pin as an output and drive
    // it to the safe OFF state. Call once at startup BEFORE the domain
    // begins (so the very first physical state is known-OFF, Phase 1 A-2).
    Status begin() {
        for (ChannelId ch = 0; ch < profile_.channelCount; ++ch) {
            const uint8_t pin = profile_.channels[ch].relayPin;
            if (pin == kNoPin) return Status::fail(Error::InvalidArgument);
            hal_.configureOutput(pin);
            writeRelay(pin, RelayState::Off);   // safe default
        }
        return Status::success();
    }

    // RelaySink: drive one logical channel. Returns false on bad channel
    // (the domain treats logical state as authoritative regardless, but
    // we report so faults can be logged — Phase 9).
    bool driveRelay(ChannelId ch, RelayState state) override {
        if (ch >= profile_.channelCount) return false;
        const uint8_t pin = profile_.channels[ch].relayPin;
        if (pin == kNoPin) return false;
        writeRelay(pin, state);
        return true;
    }

private:
    // The single place polarity is applied: map desired RelayState to the
    // physical pin level for this board's wiring.
    void writeRelay(uint8_t pin, RelayState state) {
        const bool energized = (state == RelayState::On);
        const bool pinHigh = (polarity_ == RelayPolarity::ActiveHigh)
                                 ? energized
                                 : !energized;     // active-low inverts
        hal_.writePin(pin, pinHigh);
    }

    GpioHal&             hal_;
    const DeviceProfile& profile_;
    RelayPolarity        polarity_;
};

}  // namespace ss
