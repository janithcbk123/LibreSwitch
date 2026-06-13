// =====================================================================
//  log/log_ports.h — logging seams (time source, TSDB sink)
// ---------------------------------------------------------------------
//  TimeSource: the dual-clock time the logger needs — uptime (always
//  valid, monotonic within a boot) + UTC (only after NTP sync). On
//  device: millis() + SNTP epoch. Host: injected.
//
//  TsdbSink: append a record to the TSDB ring (rollover-on). On device:
//  fdb_tsl_append_with_ts using uptime as the sort key (so records stay
//  ordered even pre-NTP — the RTC-less solution). Host: a vector mock.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include "log_types.h"

namespace ss {

class TimeSource {
public:
    virtual ~TimeSource() = default;
    virtual uint32_t uptimeMs() = 0;     // monotonic since boot, always valid
    virtual uint32_t utcEpoch() = 0;     // 0 if not NTP-synced this session
    virtual bool     ntpSynced() = 0;
};

class TsdbSink {
public:
    virtual ~TsdbSink() = default;
    // Append one encoded record, using `sortKey` (uptime) as the TSDB
    // timestamp so ordering holds without an RTC. Returns false on error.
    virtual bool append(const uint8_t* record, size_t len, uint32_t sortKey) = 0;
};

}  // namespace ss
