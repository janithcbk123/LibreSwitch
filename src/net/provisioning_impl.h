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
//  user to browse to the AP IP (192.168.4.1 default). D7-3: connectSta is
//  blocking with a timeout — AP is already down when it's called.
// =====================================================================
#pragma once

#include "provisioning_ports.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <WiFi.h>
#include <WebServer.h>

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

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
