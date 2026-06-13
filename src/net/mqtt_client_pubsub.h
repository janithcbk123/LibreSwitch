// =====================================================================
//  net/mqtt_client_pubsub.h — PubSubClient MqttClient impl (ON-DEVICE)
// ---------------------------------------------------------------------
//  The one place that touches PubSubClient + WiFiClientSecure (mTLS).
//  Compiled on-device only; the host suite uses a mock MqttClient.
//
//  TLS setup uses the same WiFiClientSecure path proven in R-0 (real
//  RSA-2048 mTLS handshake fit in RAM, ~19 KB low-water). The operational
//  client cert/key (Phase 5) + CA are installed on the secure client
//  before connect. This adapter is exclusive-TLS with OTA (Phase 3) —
//  the Mqtt task yields TLS before OTA opens its download.
//
//  Verified PubSubClient 2.8 signatures (see net/mqtt_client.h header).
// =====================================================================
#pragma once

#include "mqtt_client.h"

#if defined(LT_BUILD) || defined(ARDUINO)
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

namespace ss {

class PubSubMqttClient final : public MqttClient {
public:
    // TLS material (PEM) borrowed; must outlive the client. Installed on
    // the secure client before connect (operational cert/key, CA).
    void setTls(const char* caPem, const char* clientCertPem,
                const char* clientKeyPem) {
        if (caPem)         secure_.setCACert(caPem);
        if (clientCertPem) secure_.setCertificate(clientCertPem);
        if (clientKeyPem)  secure_.setPrivateKey(clientKeyPem);
    }

    void configure(uint16_t bufferSize, uint16_t keepAliveSec,
                   MqttInboundHandler& handler) override {
        handler_ = &handler;
        s_self = this;
        mqtt_.setClient(secure_);
        mqtt_.setBufferSize(bufferSize);          // 512 (Phase 6 D6-3)
        mqtt_.setKeepAlive(keepAliveSec);         // 30 (Phase 6 D6-4)
        mqtt_.setCallback(&PubSubMqttClient::trampoline);
    }

    void setServer(const char* host, uint16_t port) override {
        mqtt_.setServer(host, port);
    }

    bool connect(const MqttConnectParams& p) override {
        // PubSubClient takes the LWT message as a C-string; our payload is
        // bounded + NUL-safe here (small known JSON).
        char will[48];
        const size_t n = (p.lwtLen < sizeof(will)) ? p.lwtLen : sizeof(will) - 1;
        memcpy(will, p.lwtPayload, n);
        will[n] = '\0';
        return mqtt_.connect(p.clientId, p.user, p.pass,
                             p.lwtTopic, p.lwtQos, p.lwtRetain, will,
                             p.cleanSession);
    }

    bool publish(const char* topic, const uint8_t* payload,
                 size_t len, bool retained) override {
        return mqtt_.publish(topic, payload, static_cast<unsigned int>(len), retained);
    }
    bool subscribe(const char* topic, uint8_t qos) override {
        return mqtt_.subscribe(topic, qos);
    }
    bool loop() override      { return mqtt_.loop(); }
    bool connected() override { return mqtt_.connected(); }
    void disconnect() override { mqtt_.disconnect(); }
    int  lastState() override { return mqtt_.state(); }

private:
    // PubSubClient's C-style callback → route to our handler instance.
    static void trampoline(char* topic, uint8_t* payload, unsigned int len) {
        if (s_self && s_self->handler_)
            s_self->handler_->onMessage(topic, payload, len);
    }

    WiFiClientSecure   secure_;
    PubSubClient       mqtt_{secure_};
    MqttInboundHandler* handler_ = nullptr;
    static PubSubMqttClient* s_self;   // single instance (one Mqtt task)
};

inline PubSubMqttClient* PubSubMqttClient::s_self = nullptr;

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
