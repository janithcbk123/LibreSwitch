// =====================================================================
//  net/provisioning_impl.h — on-device provisioning seams (ON-DEVICE)
// ---------------------------------------------------------------------
//  Concrete WifiControl + PortalServer over LibreTiny WiFi + WebServer.
//  Compiled on-device only; host tests use mocks. Verified API:
//    WiFi.softAP(ssid, pass, channel, hidden, maxClients)
//    WiFi.softAPdisconnect(bool) / WiFi.disconnect(bool)
//    WiFi.begin(ssid, pass) -> WiFiStatus ; WiFi.status()==WL_CONNECTED(3)
//    WebServer.on(uri, method, handler) / .arg() / .send() / .handleClient()
//
//  D7-2: manual portal (no DNSServer in LibreTiny). The form tells the
//  user to browse to the AP IP — 192.168.43.1, the Beken SoftAP default
//  (ESP cores default to .4.1; we do NOT override via softAPConfig, so the
//  laptop gets a DHCP lease in 192.168.43.x). D7-3: connectSta is blocking
//  with a timeout — AP is already down when it's called.
// =====================================================================
#pragma once

#include "provisioning_ports.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "enrollment_response.h"
#include "../platform/kv_store.h"

namespace ss {

class LibreTinyWifiControl final : public WifiControl {
public:
    bool startAp(const char* ssid, const char* pass) override {
        WiFi.disconnect(false);
        // WPA2 if pass given (R-20: per-device default password).
        return WiFi.softAP(ssid, pass, /*channel*/ 1, /*hidden*/ false, /*max*/ 4);
    }
    void stopAp() override { WiFi.softAPdisconnect(true); }

    bool connectSta(const char* ssid, const char* pass, uint32_t timeoutMs) override {
        WiFi.begin(ssid, pass);
        const uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            if (WiFi.status() == WL_CONNECTED) return true;
            delay(100);
        }
        return false;
    }
    bool isStaConnected() override { return WiFi.status() == WL_CONNECTED; }
    void disconnectSta() override { WiFi.disconnect(false); }
};

class LibreTinyPortalServer final : public PortalServer {
public:
    LibreTinyPortalServer() : server_(80) {}

    void start(PortalSubmissionHandler& handler) override {
        handler_ = &handler;
        s_self = this;
        server_.on("/", HTTP_GET, &LibreTinyPortalServer::handleRoot);
        server_.on("/save", HTTP_POST, &LibreTinyPortalServer::handleSave);
        // Catch-all so OS captive-portal probes (e.g. /generate_204,
        // /hotspot-detect.html) get the form instead of a 404 — and so we
        // log that *something* connected.
        server_.onNotFound(&LibreTinyPortalServer::handleRoot);
        server_.begin();
        Serial1.print("[portal] HTTP server started; AP IP = ");
        Serial1.println(WiFi.softAPIP());          // IPAddress is Printable
    }
    void stop() override { server_.stop(); }
    void poll() override { server_.handleClient(); }
    void setMessage(const char* msg) override {
        strncpy(message_, msg, sizeof(message_) - 1);
        message_[sizeof(message_) - 1] = '\0';
    }

private:
    static void handleRoot() {
        if (!s_self) return;
        Serial1.println("[portal] HTTP request received → serving setup form");
        // Minimal config form. Manual portal (D7-2): the user reached this
        // by browsing to the AP IP. No external assets; everything inline.
        String html =
            "<!doctype html><meta name=viewport content='width=device-width'>"
            "<h2>Switch setup</h2><p>";
        html += s_self->message_;
        html += "</p><form method=POST action=/save>"
                "SSID:<br><input name=ssid maxlength=32><br>"
                "Password:<br><input name=pass type=password maxlength=63><br>"
                "Enrollment token:<br><input name=token maxlength=64><br><br>"
                "<button type=submit>Connect</button></form>";
        s_self->server_.send(200, "text/html", html);
    }
    static void handleSave() {
        if (!s_self || !s_self->handler_) return;
        ProvisioningCredentials c{};
        copyArg("ssid",  c.ssid,  sizeof(c.ssid));
        copyArg("pass",  c.pass,  sizeof(c.pass));
        copyArg("token", c.token, sizeof(c.token));
        c.valid = c.hasSsid();
        // ACK to the browser BEFORE the sequential test tears down the AP.
        s_self->server_.send(200, "text/html",
            "<h3>Testing…</h3><p>The switch will leave this network to test "
            "your Wi-Fi. Watch the LED: solid = success. If it returns to "
            "setup mode, the password was likely wrong.</p>");
        s_self->handler_->onCredentialsSubmitted(c);
    }
    static void copyArg(const char* name, char* out, size_t cap) {
        if (s_self->server_.hasArg(name)) {
            String v = s_self->server_.arg(name);
            strncpy(out, v.c_str(), cap - 1); out[cap - 1] = '\0';
        } else { out[0] = '\0'; }
    }

    WebServer server_;
    PortalSubmissionHandler* handler_ = nullptr;
    char message_[96] = "Enter your Wi-Fi details.";
    static LibreTinyPortalServer* s_self;
};

inline LibreTinyPortalServer* LibreTinyPortalServer::s_self = nullptr;

// ---------------------------------------------------------------------
//  DEV ENROLLMENT — *** R-19 placeholder, clearly labeled ***
//  The real identity bootstrap (one-time token → CSR → operational cert)
//  is a BACKEND CONTRACT that doesn't exist yet (release-gated R-19).
//  This impl stores the submitted token and reports "enrolled" if either
//  real TLS material is present in KVS (provisioned out-of-band) or a
//  token was previously accepted. It does NOT fetch a certificate; mTLS
//  remains gated on real tls_ca/tls_cert/tls_key in KVS (bringUpMqtt).
// ---------------------------------------------------------------------
class KvsDevEnrollment final : public Enrollment {
public:
    explicit KvsDevEnrollment(KvStore& kv) : kv_(kv) {}

    bool enroll(const char* token) override {
        if (!token || !*token) return false;
        return kv_.setBlob("enroll_token", token, strlen(token));
    }
    bool hasOperationalCert() override {
        uint8_t probe[4];
        if (kv_.getBlob("tls_cert", probe, sizeof(probe)) > 0) return true;   // real material
        return kv_.getBlob("enroll_token", probe, sizeof(probe)) > 0;        // dev-enrolled
    }

private:
    KvStore& kv_;
};

// ---------------------------------------------------------------------
//  HTTP ENROLLMENT — the real R-19 token→identity exchange (Option B).
//  Selected by composition_root when -D ENROLL_URL=... is set; otherwise
//  KvsDevEnrollment (above) stays. On enroll():
//    1. token comes from the portal form, else falls back to a factory-
//       seeded `enroll_token` in KVS (so the installer enters Wi-Fi only).
//    2. POST token+id+hw to ENROLL_URL over SERVER-AUTH TLS. The bootstrap
//       CA (ENROLL_CA_PEM) PINS the enrollment server; with no CA we REFUSE
//       to send the token unless -D ENROLL_INSECURE is set (dev only) —
//       an unauthenticated server could harvest the token + hand back a
//       rogue identity.
//    3. parse the response (enrollment_response.h) and persist the per-
//       device identity (tls_ca/tls_cert/tls_key + mqtt_host[/root][/id]).
//       bringUpMqtt picks it up and connects on the NEXT boot.
//  Runs on the Net task right after the sequential STA join succeeds
//  (provisioning_service.h), where a blocking HTTP exchange is fine and
//  cannot touch local control. Hand-rolled GET/POST over WiFiClientSecure
//  — mirrors HttpDownloader, avoids the HTTPClient link failure.
// ---------------------------------------------------------------------
class HttpEnrollment final : public Enrollment {
public:
    HttpEnrollment(KvStore& kv, const char* url, const char* bootstrapCa,
                   const char* hwFamily, const char* deviceHint)
        : kv_(kv), url_(url ? url : ""), ca_(bootstrapCa ? bootstrapCa : ""),
          hw_(hwFamily ? hwFamily : ""), hint_(deviceHint ? deviceHint : "") {}

    bool enroll(const char* token) override {
        if (hasOperationalCert()) return true;       // idempotent — already have identity

        char seeded[kMaxToken] = {};                 // factory-seeded fallback
        if (!token || !*token) {
            if (kv_.getBlob("enroll_token", seeded, sizeof(seeded)) > 0) token = seeded;
        }
        if (!token || !*token) { Serial1.println("[enroll] no token (form or factory seed)"); return false; }
        if (!url_[0])          { Serial1.println("[enroll] ENROLL_URL not configured"); return false; }

        if (!ca_[0]) {
#if defined(ENROLL_INSECURE)
            Serial1.println("[enroll] WARNING: ENROLL_INSECURE — enrollment server NOT authenticated (dev only)");
#else
            Serial1.println("[enroll] REFUSING: no pinned ENROLL_CA_PEM "
                            "(set it, or -D ENROLL_INSECURE for dev)");
            return false;
#endif
        }
        return doEnroll(token);
    }

    bool hasOperationalCert() override {
        uint8_t probe[4];
        return kv_.getBlob("tls_cert", probe, sizeof(probe)) > 0;   // real material only
    }

private:
    bool doEnroll(const char* token) {
        char host[128]; uint16_t port; char path[160];
        if (!parseUrl(url_, host, sizeof(host), port, path, sizeof(path))) {
            Serial1.println("[enroll] bad ENROLL_URL (need https://host[:port]/path)"); return false;
        }

        char body[256];
        const int blen = snprintf(body, sizeof(body), "token=%s&id=%s&hw=%s", token, hint_, hw_);
        if (blen < 0 || static_cast<size_t>(blen) >= sizeof(body)) {
            Serial1.println("[enroll] request body too long"); return false;
        }

        WiFiClientSecure secure;
        if (ca_[0]) secure.setCACert(ca_);
        if (secure.connect(host, port) != 1) { Serial1.println("[enroll] TLS connect failed"); return false; }

        secure.print("POST ");   secure.print(path); secure.print(" HTTP/1.1\r\n");
        secure.print("Host: ");  secure.print(host); secure.print("\r\n");
        secure.print("Content-Type: application/x-www-form-urlencoded\r\n");
        secure.print("Content-Length: "); secure.print(blen); secure.print("\r\n");
        secure.print("Connection: close\r\n\r\n");
        secure.print(body);

        char line[256];
        if (!readLine(&secure, line, sizeof(line))) { secure.stop(); return false; }
        const char* sp = strchr(line, ' ');
        if (!sp || atoi(sp + 1) != 200) {
            Serial1.print("[enroll] HTTP status: "); Serial1.println(line); secure.stop(); return false;
        }
        for (;;) {                                   // skip headers to blank line
            if (!readLine(&secure, line, sizeof(line))) { secure.stop(); return false; }
            if (line[0] == '\0') break;
        }

        const size_t n = readBody(&secure, resp_, sizeof(resp_));
        secure.stop();
        if (n == 0) { Serial1.println("[enroll] empty response body"); return false; }

        const EnrollmentResponse er = EnrollmentResponseCodec::parse(resp_, n);
        if (!er.valid) {
            Serial1.println("[enroll] BAD response (need @@tls_ca,@@tls_cert,@@tls_key,@@mqtt_host)");
            return false;
        }
        return store(er);
    }

    // Persist the identity. PEMs must fit the 1800 B KVS read buffers in the
    // composition root (leave room for the implicit NUL → store ≤ 1799).
    bool store(const EnrollmentResponse& e) {
        if (e.tlsCa.len > 1799 || e.tlsCert.len > 1799 || e.tlsKey.len > 1799) {
            Serial1.println("[enroll] cert/key exceeds 1800 B KVS buffer — rejected"); return false;
        }
        bool ok = true;
        ok &= kv_.setBlob("tls_ca",    e.tlsCa.ptr,    e.tlsCa.len);
        ok &= kv_.setBlob("tls_cert",  e.tlsCert.ptr,  e.tlsCert.len);
        ok &= kv_.setBlob("tls_key",   e.tlsKey.ptr,   e.tlsKey.len);
        ok &= kv_.setBlob("mqtt_host", e.mqttHost.ptr, e.mqttHost.len);
        if (e.mqttRoot.present()) ok &= kv_.setBlob("mqtt_root", e.mqttRoot.ptr, e.mqttRoot.len);
        if (e.deviceId.present()) ok &= kv_.setBlob("device_id", e.deviceId.ptr, e.deviceId.len);
        Serial1.println(ok ? "[enroll] identity stored — MQTT connects on next boot"
                           : "[enroll] KVS write failed");
        return ok;
    }

    // scheme://host[:port]/path → host/port/path. Mirrors HttpDownloader.
    static bool parseUrl(const char* url, char* host, size_t hostCap,
                         uint16_t& port, char* path, size_t pathCap) {
        bool tls = false; size_t off = 0;
        if      (!strncmp(url, "https://", 8)) { tls = true;  off = 8; }
        else if (!strncmp(url, "http://",  7)) { tls = false; off = 7; }
        else return false;
        port = tls ? 443 : 80;
        const char* p = url + off;
        const char* slash = strchr(p, '/');
        const char* colon = strchr(p, ':');
        const char* hostEnd = slash ? slash : (p + strlen(p));
        if (colon && colon < hostEnd) {
            size_t hlen = static_cast<size_t>(colon - p);
            if (hlen + 1 > hostCap) return false;
            memcpy(host, p, hlen); host[hlen] = 0;
            port = static_cast<uint16_t>(atoi(colon + 1));
        } else {
            size_t hlen = static_cast<size_t>(hostEnd - p);
            if (hlen + 1 > hostCap) return false;
            memcpy(host, p, hlen); host[hlen] = 0;
        }
        if (slash) { if (strlen(slash) + 1 > pathCap) return false; strcpy(path, slash); }
        else { if (pathCap < 2) return false; strcpy(path, "/"); }
        return true;
    }

    // Read the response body until the server closes (Connection: close)
    // or the buffer fills. Returns bytes read.
    static size_t readBody(Client* c, char* out, size_t cap) {
        size_t total = 0; uint32_t idleMs = 0;
        while (total + 1 < cap && idleMs < kIdleTimeoutMs) {
            const int avail = c->available();
            if (avail <= 0) {
                if (!c->connected() && c->available() <= 0) break;
                delay(10); idleMs += 10; continue;
            }
            idleMs = 0;
            size_t want = static_cast<size_t>(avail);
            if (want > cap - 1 - total) want = cap - 1 - total;
            const int rd = c->read(reinterpret_cast<uint8_t*>(out + total), want);
            if (rd <= 0) break;
            total += static_cast<size_t>(rd);
        }
        out[total] = '\0';
        return total;
    }

    // One CRLF-terminated line (CR/LF stripped). false on timeout/close.
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
            if (ch == '\n') { out[i] = 0; if (i && out[i-1] == '\r') out[i-1] = 0; return true; }
            out[i++] = static_cast<char>(ch);
        }
        out[i < cap ? i : cap - 1] = 0;
        return i > 0;
    }

    static constexpr uint32_t kIdleTimeoutMs = 15000;

    KvStore&    kv_;
    const char* url_;
    const char* ca_;
    const char* hw_;
    const char* hint_;
    char        resp_[EnrollmentResponseCodec::kMaxResponse] = {};  // static storage (net task stack is small)
};

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
