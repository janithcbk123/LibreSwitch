// =====================================================================
//  log/log_types.h — log event types + record codec (pure)
// ---------------------------------------------------------------------
//  Phase 9. Structured binary event records (~20 B) for density in the
//  116 KB TSDB ring (~5000+ records). Enumerated codes, not free text.
//  Dual-clock fields (uptime always + UTC-when-synced) for the RTC-less
//  hardware. Pure encode/decode → host-testable; the TSDB append is a
//  seam. Reader reconstructs wall-clock from anchor events (backend).
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ss {

enum class Severity : uint8_t {
    Debug = 0, Info = 1, Warn = 2, Error = 3, Critical = 4
};

// Event codes, categorized (Phase 9 §1). Range = category.
enum class EventCode : uint16_t {
    // 0x0xxx boot / lifecycle
    Boot              = 0x0001,
    ResetReason       = 0x0002,
    FactoryReset      = 0x0003,
    ProfileLoaded     = 0x0004,
    // 0x1xxx connectivity
    WifiUp            = 0x1001,
    WifiDown          = 0x1002,
    MqttConnected     = 0x1003,
    MqttDisconnected  = 0x1004,
    NtpSync           = 0x1005,   // also the dual-clock ANCHOR event
    CertRefresh       = 0x1006,
    CertExpiryWarning = 0x1007,
    // 0x2xxx control
    RelayOn           = 0x2001,
    RelayOff          = 0x2002,
    TouchEvent        = 0x2003,
    BootRestore       = 0x2004,
    // 0x3xxx OTA
    OtaStart          = 0x3001,
    OtaVerifyOk       = 0x3002,
    OtaVerifyFail     = 0x3003,
    OtaCommit         = 0x3004,
    OtaRollback       = 0x3005,
    OtaConfirmed      = 0x3006,
    // 0x4xxx security
    Provisioned       = 0x4001,
    Enrolled          = 0x4002,
    AuthFail          = 0x4003,
    UnauthorizedCmd   = 0x4004,
    // 0x5xxx faults
    HeapLow           = 0x5001,
    WatchdogBite      = 0x5002,
    TaskStall         = 0x5003,
    FlashError        = 0x5004,
    TlsError          = 0x5005,
};

// Record flag bits.
enum : uint8_t {
    kFlagClockValid = 0x01,   // ts_utc is real (NTP-synced this session)
    kFlagAnchor     = 0x02,   // this record is an NTP anchor (both clocks real)
};

// On-flash record. Packed, fixed 20 B. boot_id stitches sessions across
// reboots (uptime resets each boot); the reader orders by (boot_id,uptime).
#pragma pack(push, 1)
struct LogRecord {
    uint32_t ts_utc;     // epoch secs, or 0 if pre-NTP
    uint32_t uptime_ms;  // monotonic since boot — ALWAYS valid
    uint16_t boot_id;    // increments per boot (KVS counter)
    uint16_t code;       // EventCode
    uint16_t arg1;
    uint16_t arg2;
    uint8_t  severity;   // Severity
    uint8_t  flags;
    uint8_t  _pad[2];    // → 20 bytes, aligned
};
#pragma pack(pop)
static_assert(sizeof(LogRecord) == 20, "LogRecord must be 20 bytes");

// Codec — trivial memcpy (fixed layout), but wrapped for testability and
// a single place to evolve the schema (versioning lives in the TSDB name).
class LogCodec {
public:
    static void encode(const LogRecord& r, uint8_t out[sizeof(LogRecord)]) {
        memcpy(out, &r, sizeof(LogRecord));
    }
    static bool decode(const uint8_t* in, size_t len, LogRecord& out) {
        if (len != sizeof(LogRecord)) return false;
        memcpy(&out, in, sizeof(LogRecord));
        return true;
    }
};

}  // namespace ss
