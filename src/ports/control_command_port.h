// =====================================================================
//  ports/control_command_port.h — inbound port (adapters → Core Domain)
// ---------------------------------------------------------------------
//  THE single inbound seam for commanding the switch. Every command
//  source — MQTT (Phase 6), optional LAN REST (Phase 3), boot policy —
//  enters through this one interface (Phase 2). The ControlCoordinator
//  implements it. This is what makes "MQTT never writes a relay directly"
//  structurally true: connectivity adapters can only ask via this port,
//  and the coordinator (owned by the Control task) decides + acts.
//
//  Pure: types only, no platform. Commands carry their source so the
//  coordinator can apply policy (e.g. local authority) and logging
//  (Phase 9 "src" field) can attribute them.
// =====================================================================
#pragma once

#include "../domain/relay_types.h"
#include "../domain/result.h"

namespace ss {

// What a caller wants done. Kept minimal and explicit — no free-form
// payloads in the domain (parsing/validation happens in the adapter
// before it reaches this port).
enum class CommandKind : uint8_t {
    SetChannel = 0,   // set one channel to `state`
    ToggleChannel = 1,
    SetAll = 2,       // set every present channel to `state`
};

struct ControlCommand {
    CommandKind   kind   = CommandKind::SetChannel;
    ChannelId     channel = 0;                 // used by SetChannel/Toggle
    RelayState    state  = RelayState::Off;    // used by SetChannel/SetAll
    CommandSource source = CommandSource::Internal;
};

// Inbound port. Synchronous: the caller (an adapter, on the Control
// task via the command queue) gets a structured Status back.
class ControlCommandPort {
public:
    virtual ~ControlCommandPort() = default;
    virtual Status submit(const ControlCommand& cmd) = 0;
};

}  // namespace ss
