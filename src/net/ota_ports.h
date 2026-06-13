// =====================================================================
//  net/ota_ports.h — OTA seams (writer, verifier, downloader, reboot)
// ---------------------------------------------------------------------
//  The OTA orchestration is host-testable; the I/O + crypto are behind
//  these seams. On-device impls wrap LibreTiny Update (flash write +
//  rollBack), mbedTLS Ed25519, and WiFiClientSecure HTTPS.
//
//  Key safety property the seams encode (D8-1/D8-4): a download goes to
//  the SEPARATE download slot; the running app is untouched until commit
//  activates the image. "rollback" = erase the staged slot (lt_ota_switch
//  revert), which on BK7231N reverts to the still-intact current app.
// =====================================================================
#pragma once

#include "ota_types.h"

namespace ss {

// Streams the image into the download slot. begin(size) → write(chunk)* →
// finish(). abort() erases the partial slot (running app safe).
class FirmwareWriter {
public:
    virtual ~FirmwareWriter() = default;
    virtual bool begin(uint32_t size) = 0;
    virtual bool writeChunk(const uint8_t* data, size_t len) = 0;
    virtual bool finish() = 0;          // finalize staged image (RBL/CRC)
    virtual void abort() = 0;           // erase staged slot
    virtual bool commit() = 0;          // activate staged image for next boot
    virtual bool rollBack() = 0;        // erase staged image (revert)
};

// Ed25519 signature verification over the staged image (D8-2 layer 2).
// On-device: mbedTLS, pinned public key compiled into firmware (NOT in
// KVS — must not be attacker-replaceable). Verify happens BEFORE commit.
class SignatureVerifier {
public:
    virtual ~SignatureVerifier() = default;
    // Verify the signature (base64) over the staged image. Returns true
    // only on a valid signature from the pinned key.
    virtual bool verifyStagedImage(const char* sigB64) = 0;
};

// Reports download progress / streams chunks to a writer.
class DownloadSink {
public:
    virtual ~DownloadSink() = default;
    virtual bool onChunk(const uint8_t* data, size_t len) = 0;
};

// Streams an image from a URL over mTLS to the sink. Returns true if the
// full image was delivered. Exclusive-TLS with MQTT (Phase 3): the caller
// ensures MQTT yielded TLS before this runs.
class Downloader {
public:
    virtual ~Downloader() = default;
    virtual bool download(const char* url, uint32_t expectedSize, DownloadSink& sink) = 0;
};

class RebootControl {
public:
    virtual ~RebootControl() = default;
    virtual void reboot() = 0;
};

}  // namespace ss
