// =====================================================================
//  domain/crc16.h — CRC-16/CCITT (pure utility)
// ---------------------------------------------------------------------
//  Small, dependency-free integrity check used to detect corrupt/partial
//  persisted records (so they read back as invalid → safe fallback,
//  e.g. BootPolicy R-7). Pure + host-testable. CCITT-FALSE (poly 0x1021,
//  init 0xFFFF) — standard, adequate for flash-record integrity.
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>

namespace ss {

inline uint16_t crc16(const void* data, size_t len, uint16_t crc = 0xFFFF) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(p[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

}  // namespace ss
