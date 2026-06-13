// =====================================================================
//  domain/result.h — structured error + result types (Core Domain)
// ---------------------------------------------------------------------
//  Phase 2 ErrorModel: command/query separation, structured errors (no
//  bare bools or magic ints). Pure, header-only, host-testable, zero heap.
//  No platform, no RTOS, no Arduino includes here — Core Domain is pure.
// =====================================================================
#pragma once

#include <cstdint>

namespace ss {

// Enumerated, stable error codes. Logged as uint16 (Phase 9 schema).
// Ranges mirror the Phase 9 event-code categories for traceability.
enum class Error : uint16_t {
    None = 0,

    // 0x01xx — argument / domain validation
    InvalidChannel       = 0x0101,
    ChannelNotPresent    = 0x0102,  // valid index, but not on this profile
    InvalidArgument      = 0x0103,

    // 0x02xx — state / precondition
    NotInitialized       = 0x0201,
    AlreadyInitialized   = 0x0202,
    Busy                 = 0x0203,

    // 0x05xx — fault
    InternalInvariant    = 0x0501,  // a "can't happen" guard tripped
};

// Command result: did a state-changing operation succeed? (command/query
// separation — commands return Status, queries return values).
struct Status {
    Error error = Error::None;

    constexpr bool ok() const { return error == Error::None; }
    constexpr explicit operator bool() const { return ok(); }

    static constexpr Status success() { return Status{Error::None}; }
    static constexpr Status fail(Error e) { return Status{e}; }
};

constexpr bool operator==(Status a, Status b) { return a.error == b.error; }
constexpr bool operator!=(Status a, Status b) { return a.error != b.error; }

}  // namespace ss
