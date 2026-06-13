// =====================================================================
//  net/mqtt_client.h — MQTT client seam (Platform/Net layer)
// ---------------------------------------------------------------------
//  The thin seam over PubSubClient + WiFiClientSecure. The MqttService
//  (connection lifecycle, routing, publishing) is written against THIS
//  interface, so all that logic is host-testable with a mock; only the
//  concrete PubSubClient/TLS implementation needs the device.
//
//  Verified PubSubClient 2.8 API this maps to:
//    connect(id,user,pass,willTopic,willQos,willRetain,willMsg,cleanSession)
//    publish(topic,payload,retained) / subscribe(topic,qos)
//    loop() / connected() / setBufferSize() / setKeepAlive()
//    callback: void(char* topic, uint8_t* payload, unsigned int len)
// =====================================================================
#pragma once

#include <cstdint>
#include <cstddef>

namespace ss {

// Inbound message handler — the service registers this to receive
// messages PubSubClient delivers in loop(). topic is NUL-terminated;
// payload has explicit length (NOT NUL-terminated).
class MqttInboundHandler {
public:
    virtual ~MqttInboundHandler() = default;
    virtual void onMessage(const char* topic, const uint8_t* payload, size_t len) = 0;
};

// Connection parameters for a connect attempt, incl. LWT + clean session
// (Phase 6 D6-4). All strings borrowed for the duration of the call.
struct MqttConnectParams {
    const char* clientId;
    const char* user;          // may be null
    const char* pass;          // may be null
    const char* lwtTopic;      // evt/system/online
    const uint8_t* lwtPayload; // {"online":false}
    size_t      lwtLen;
    uint8_t     lwtQos;        // 1
    bool        lwtRetain;     // true
    bool        cleanSession;  // true (Phase 6 D6-4)
};

class MqttClient {
public:
    virtual ~MqttClient() = default;

    // One-time setup: buffer size (512, Phase 6 D6-3), keepalive (30s),
    // inbound handler, server host/port.
    virtual void configure(uint16_t bufferSize, uint16_t keepAliveSec,
                           MqttInboundHandler& handler) = 0;
    virtual void setServer(const char* host, uint16_t port) = 0;

    // Attempt a connect (with LWT + clean session). Returns true on success.
    virtual bool connect(const MqttConnectParams& p) = 0;

    virtual bool publish(const char* topic, const uint8_t* payload,
                         size_t len, bool retained) = 0;
    virtual bool subscribe(const char* topic, uint8_t qos) = 0;

    // Must be pumped regularly to service I/O + deliver inbound messages.
    virtual bool loop() = 0;
    virtual bool connected() = 0;

    virtual void disconnect() = 0;
    virtual int  lastState() = 0;     // PubSubClient state() code (diag)
};

}  // namespace ss
