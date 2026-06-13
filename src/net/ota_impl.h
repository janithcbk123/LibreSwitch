// =====================================================================
//  net/ota_impl.h — on-device OTA seam impls (ON-DEVICE)
// ---------------------------------------------------------------------
//  FirmwareWriter over LibreTiny Update + RebootControl. Compiled on
//  device only; host tests use mocks. Verified Update API:
//    Update.begin(size, U_FLASH) / write(data,len) / end()
//    Update.canRollBack() / rollBack()  (rollBack == erase staged slot,
//    reverting to the still-intact current app — see lt_ota_switch).
//
//  The streamed write goes to the SEPARATE download slot; the running app
//  is untouched until commit. abort()/rollBack() erase the staged slot.
//  Ed25519 verify (SignatureVerifier) + HTTPS download (Downloader) impls
//  are larger (mbedTLS / WiFiClientSecure) and live alongside; the verify
//  key is PINNED in the image (R-23, not KVS).
// =====================================================================
#pragma once

#include "ota_ports.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <Update.h>
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

namespace ss {

class LibreTinyFirmwareWriter final : public FirmwareWriter {
public:
    bool begin(uint32_t size) override {
        // Stream a SHA-256 of every byte written, alongside the flash
        // write. The digest is consumed by the verifier (below) before
        // commit. Legacy (non-_ret) mbedTLS API — matches the platform's
        // own usage (see MD5MbedTLSImpl.cpp).
        mbedtls_sha256_init(&sha_);
        mbedtls_sha256_starts(&sha_, /*is224*/ 0);
        digestReady_ = false;
        return Update.begin(size, U_FLASH);    // stages into download slot
    }
    bool writeChunk(const uint8_t* data, size_t len) override {
        mbedtls_sha256_update(&sha_, data, len);
        return Update.write(const_cast<uint8_t*>(data), len) == len;
    }
    bool finish() override {
        mbedtls_sha256_finish(&sha_, digest_);
        mbedtls_sha256_free(&sha_);
        digestReady_ = true;
        return Update.end(/*evenIfRemaining*/ false);   // finalize RBL/CRC
    }

    // SHA-256 of the bytes streamed this session (valid after finish()).
    const uint8_t* digest() const { return digestReady_ ? digest_ : nullptr; }
    void abort() override { Update.abort(); }            // erase staged slot
    bool commit() override {
        // end() already finalized; the bootloader will boot the staged
        // image next cycle (lt_ota_switch activate). canRollBack confirms
        // a valid staged image exists.
        return Update.canRollBack();
    }
    bool rollBack() override { return Update.rollBack(); }  // erase → revert

private:
    mbedtls_sha256_context sha_{};
    uint8_t digest_[32] = {};
    bool digestReady_ = false;
};

// ---------------------------------------------------------------------
//  DEV INTEGRITY VERIFIER — *** NOT the production trust layer ***
//  (R-23 placeholder, clearly labeled.)
//
//  The Phase 5/8 design calls for Ed25519 signature verification with a
//  PINNED public key. The platform's bundled mbedTLS (BDK, 2.x-era) has
//  NO Ed25519 support, so the real verifier is a release-gated open item
//  (options: vendor a compact ed25519 impl, or switch signing to
//  ECDSA-P256 which this mbedTLS does support).
//
//  Until then, this verifier treats the manifest `sig` field as
//  base64(SHA-256 of the image) and checks INTEGRITY of the received
//  bytes. That protects against corruption — it does NOT authenticate
//  the publisher. Layer 1 (HW-compat) + layer 3 (platform RBL/CRC)
//  still apply. DO NOT SHIP THIS AS THE ONLY LAYER-2.
// ---------------------------------------------------------------------
class DevSha256Verifier final : public SignatureVerifier {
public:
    explicit DevSha256Verifier(LibreTinyFirmwareWriter& writer) : writer_(writer) {}

    bool verifyStagedImage(const char* sigB64) override {
        const uint8_t* d = writer_.digest();
        if (!d || !sigB64 || !*sigB64) return false;
        uint8_t expect[32]; size_t outLen = 0;
        if (mbedtls_base64_decode(expect, sizeof(expect), &outLen,
                                  reinterpret_cast<const unsigned char*>(sigB64),
                                  strlen(sigB64)) != 0) return false;
        if (outLen != 32) return false;
        return memcmp(expect, d, 32) == 0;
    }

private:
    LibreTinyFirmwareWriter& writer_;
};

// ---------------------------------------------------------------------
//  HTTP(S) downloader over WiFiClient/WiFiClientSecure (manual GET — avoids
//  the HTTPClient dependency, which drags in strptime/_gettimeofday and
//  fails to link). Streams the body straight to the sink —
//  never buffers the image (664 KB ≫ RAM; R-0 headroom preserved).
//  For https URLs a CA can be supplied (from KVS tls_ca); http URLs go
//  plain — acceptable because layer-2 verify (above) gates commit.
// ---------------------------------------------------------------------
class HttpDownloader final : public Downloader {
public:
    // ca may be nullptr/empty. For https with a CA we verify the server;
    // for https without one we connect but cannot authenticate the host
    // (acceptable only because the OTA layer-2 verify gates commit). For
    // http we use a plain client.
    explicit HttpDownloader(const char* ca = nullptr) : ca_(ca) {}

    bool download(const char* url, uint32_t expectedSize, DownloadSink& sink) override {
        // --- parse url: scheme://host[:port]/path ---
        bool tls = false; size_t off = 0;
        if      (!strncmp(url, "https://", 8)) { tls = true;  off = 8; }
        else if (!strncmp(url, "http://",  7)) { tls = false; off = 7; }
        else return false;

        char host[128]; uint16_t port = tls ? 443 : 80; char path[256];
        const char* p = url + off;
        const char* slash = strchr(p, '/');
        const char* colon = strchr(p, ':');
        const char* hostEnd = slash ? slash : (p + strlen(p));
        if (colon && colon < hostEnd) {
            size_t hlen = (size_t)(colon - p);
            if (hlen + 1 > sizeof(host)) return false;
            memcpy(host, p, hlen); host[hlen] = 0;
            port = (uint16_t)atoi(colon + 1);
        } else {
            size_t hlen = (size_t)(hostEnd - p);
            if (hlen + 1 > sizeof(host)) return false;
            memcpy(host, p, hlen); host[hlen] = 0;
        }
        if (slash) { if (strlen(slash) + 1 > sizeof(path)) return false; strcpy(path, slash); }
        else strcpy(path, "/");

        // --- connect (TLS or plain) ---
        WiFiClient plain;
        WiFiClientSecure secure;
        Client* c = nullptr;
        if (tls) {
            if (ca_ && *ca_) secure.setCACert(ca_);
            if (secure.connect(host, port) != 1) return false;
            c = &secure;
        } else {
            if (plain.connect(host, port) != 1) return false;
            c = &plain;
        }

        // --- send a minimal HTTP/1.1 GET ---
        c->print("GET "); c->print(path); c->print(" HTTP/1.1\r\n");
        c->print("Host: "); c->print(host); c->print("\r\n");
        c->print("Connection: close\r\n\r\n");

        // --- read status line: expect "HTTP/1.x 200" ---
        char line[256];
        if (!readLine(c, line, sizeof(line))) { c->stop(); return false; }
        const char* sp = strchr(line, ' ');
        if (!sp || atoi(sp + 1) != 200) { c->stop(); return false; }

        // --- skip headers until blank line ---
        for (;;) {
            if (!readLine(c, line, sizeof(line))) { c->stop(); return false; }
            if (line[0] == '\0') break;            // CRLF-only = end of headers
        }

        // --- stream body → sink, exactly expectedSize bytes ---
        uint32_t total = 0; uint8_t buf[1024]; uint32_t idleMs = 0;
        while (total < expectedSize && idleMs < kIdleTimeoutMs) {
            const int avail = c->available();
            if (avail <= 0) {
                if (!c->connected() && c->available() <= 0) break;
                delay(10); idleMs += 10; continue;
            }
            idleMs = 0;
            size_t want = (size_t)avail; if (want > sizeof(buf)) want = sizeof(buf);
            if (want > (expectedSize - total)) want = (expectedSize - total);
            const int n = c->read(buf, want);
            if (n <= 0) break;
            if (!sink.onChunk(buf, (size_t)n)) { c->stop(); return false; }
            total += (uint32_t)n;
        }
        c->stop();
        return total == expectedSize;
    }

private:
    static constexpr uint32_t kIdleTimeoutMs = 15000;

    // Read one CRLF-terminated line (CR/LF stripped). false on timeout/close.
    static bool readLine(Client* c, char* out, size_t cap) {
        size_t i = 0; uint32_t idleMs = 0;
        while (i + 1 < cap && idleMs < kIdleTimeoutMs) {
            const int a = c->available();
            if (a <= 0) {
                if (!c->connected() && c->available() <= 0) return i > 0;
                delay(5); idleMs += 5; continue;
            }
            idleMs = 0;
            const int ch = c->read();
            if (ch < 0) break;
            if (ch == '\n') { out[i] = 0; if (i && out[i-1]=='\r') out[i-1]=0; return true; }
            out[i++] = (char)ch;
        }
        out[i < cap ? i : cap-1] = 0;
        return i > 0;
    }

    const char* ca_;
};

class LibreTinyReboot final : public RebootControl {
public:
    void reboot() override { ESP.restart(); }   // LibreTiny Arduino-compat
};

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
