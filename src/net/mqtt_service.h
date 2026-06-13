// =====================================================================
//  net/mqtt_service.h — MqttService (Connectivity adapter)
// ---------------------------------------------------------------------
//  The MQTT adapter proper. Ties together the seam (MqttClient), the
//  topic builder, the payload codec, and the backoff into the Phase 6
//  behaviour. It:
//    • implements MqttInboundHandler — parses cmd/* topics and submits
//      ControlCommands through ControlCommandPort (the coordinator). MQTT
//      NEVER writes a relay directly — it can only request via the port,
//      and the Control task performs the action (Phase 1/2 invariant).
//    • implements StateChangePort — relay changes are queued and
//      published as retained evt/relay/<ch>/state (telemetry dedupe is
//      already done by the coordinator; queue is drop-oldest, Phase 3).
//    • manages connection lifecycle — connect w/ LWT + clean session,
//      subscribe to cmd/#, reconnect with backoff+jitter.
//
//  Runs in the Mqtt task (priority 2). All logic here is host-testable
//  against a mock MqttClient + a mock ControlCommandPort; only the
//  PubSubClient/TLS impl is on-device.
// =====================================================================
#pragma once

#include "mqtt_client.h"
#include "mqtt_topics.h"
#include "mqtt_payload.h"
#include "reconnect_backoff.h"
#include "../ports/control_command_port.h"
#include "../ports/state_change_port.h"
#include "../platform/bounded_queue.h"

namespace ss {

// A pending outbound state publish (queued from the Control task's
// announce path, drained by the Mqtt task). Small POD, no heap.
struct PendingState {
    ChannelId     ch;
    RelayState    state;
    CommandSource source;
};

inline constexpr size_t kPublishQueueDepth = 16;   // Phase 3 telemetry(16)
using PublishQueue = BoundedQueue<PendingState, kPublishQueueDepth>;

class MqttService final : public MqttInboundHandler, public StateChangePort {
public:
    MqttService(MqttClient& client, ControlCommandPort& commandPort,
                const char* root, const char* deviceId, const char* clientId)
        : client_(client), commandPort_(commandPort),
          topics_(root, deviceId), clientId_(clientId),
          publishQ_(OverflowPolicy::DropOldest) {}

    void begin(MqttInboundHandler*& /*unused*/) {}   // reserved

    void configure() {
        client_.configure(/*buffer*/ 512, /*keepalive s*/ 30, *this);
    }
    void setServer(const char* host, uint16_t port) { client_.setServer(host, port); }

    // ---- StateChangePort: relay change → queue for publish ----
    void onStateChanged(ChannelId ch, RelayState state, CommandSource src) override {
        publishQ_.push({ch, state, src});            // drop-oldest if full
    }

    // Optional sink for system commands (OTA manifest / health confirm).
    // Wired by the composition root; absent → system commands ignored.
    class SystemCommandSink {
    public:
        virtual ~SystemCommandSink() = default;
        virtual void onOtaManifest(const uint8_t* payload, size_t len) = 0;
        virtual void onOtaConfirm() = 0;
    };
    void setSystemSink(SystemCommandSink& s) { sys_ = &s; }

    // ---- MqttInboundHandler: route incoming cmd/* → ControlCommandPort ----
    void onMessage(const char* topic, const uint8_t* payload, size_t len) override {
        if (MqttTopics::isSystemOta(topic))     { if (sys_) sys_->onOtaManifest(payload, len); return; }
        if (MqttTopics::isSystemConfirm(topic)) { if (sys_) sys_->onOtaConfirm(); return; }
        if (MqttTopics::isRelayAllSet(topic)) { routeAllSet(payload, len); return; }
        ChannelId ch;
        if (MqttTopics::parseRelaySetChannel(topic, ch)) routeChannelSet(ch, payload, len);
        // unknown topics ignored (defensive)
    }

    // Yield the broker connection (frees TLS/socket RAM before an OTA
    // download — Phase 3 exclusive-TLS). Reconnect happens via the normal
    // backoff on subsequent ticks.
    void yieldConnection() { client_.disconnect(); }

    // ---- lifecycle: call each Mqtt-task cycle ----
    void tick(Millis now, uint32_t jitterSeed, uint32_t tsForPublish) {
        if (client_.connected()) {
            client_.loop();                  // service I/O + deliver inbound
            drainPublishes(tsForPublish);
            return;
        }
        // not connected → reconnect per backoff
        if (backoff_.ready(now)) attemptConnect(now, jitterSeed);
    }

    bool isConnected() { return client_.connected(); }

private:
    void attemptConnect(Millis now, uint32_t jitterSeed) {
        // NOTE: lwtTopic/lwt are call-scoped locals. This is correct: the
        // MqttClient consumes them synchronously within connect() (the
        // PubSubClient impl copies topic+payload into its own buffer during
        // the call). Do NOT hoist these to borrowed members thinking it's a
        // lifetime fix — they must simply outlive the connect() call, which
        // they do. (A host test that captures the pointer after the call
        // sees a dangling local — the mock must copy in-call, as the real
        // client does.)
        char lwtTopic[kMaxTopicLen];
        topics_.evtOnline(lwtTopic, sizeof(lwtTopic));
        char lwt[32];
        const size_t lwtLen = MqttPayload::online(lwt, sizeof(lwt), false);

        MqttConnectParams p{};
        p.clientId = clientId_;
        p.lwtTopic = lwtTopic;
        p.lwtPayload = reinterpret_cast<const uint8_t*>(lwt);
        p.lwtLen = lwtLen;
        p.lwtQos = 1; p.lwtRetain = true;
        p.cleanSession = true;               // Phase 6 D6-4

        if (client_.connect(p)) {
            onConnected();
            backoff_.onSuccess();
        } else {
            backoff_.onFailure(now, jitterSeed);
        }
    }

    void onConnected() {
        // announce online (retained), then subscribe to all commands.
        char topic[kMaxTopicLen]; char buf[32];
        topics_.evtOnline(topic, sizeof(topic));
        const size_t n = MqttPayload::online(buf, sizeof(buf), true);
        client_.publish(topic, reinterpret_cast<const uint8_t*>(buf), n, /*retain*/ true);

        topics_.cmdWildcard(topic, sizeof(topic));
        client_.subscribe(topic, 1);
    }

    void drainPublishes(uint32_t ts) {
        PendingState ps;
        size_t budget = publishQ_.capacity();
        while (budget-- > 0 && publishQ_.pop(ps)) publishState(ps, ts);
    }

    void publishState(const PendingState& ps, uint32_t ts) {
        char topic[kMaxTopicLen];
        if (!topics_.evtRelayState(topic, sizeof(topic), ps.ch)) return;
        char buf[64];
        const size_t n = MqttPayload::relayState(buf, sizeof(buf), ps.state, ps.source, ts);
        if (n) client_.publish(topic, reinterpret_cast<const uint8_t*>(buf), n, /*retain*/ true);
    }

    void routeChannelSet(ChannelId ch, const uint8_t* payload, size_t len) {
        bool on;
        if (!MqttPayload::parseRelaySet(payload, len, on)) return;   // ignore malformed
        commandPort_.submit({CommandKind::SetChannel, ch,
                             on ? RelayState::On : RelayState::Off, CommandSource::Cloud});
    }

    void routeAllSet(const uint8_t* payload, size_t len) {
        bool on;
        if (!MqttPayload::parseRelaySet(payload, len, on)) return;
        commandPort_.submit({CommandKind::SetAll, 0,
                             on ? RelayState::On : RelayState::Off, CommandSource::Cloud});
    }

    MqttClient&         client_;
    ControlCommandPort& commandPort_;
    MqttTopics          topics_;
    const char*         clientId_;
    PublishQueue        publishQ_;
    SystemCommandSink* sys_ = nullptr;
    ReconnectBackoff    backoff_;
};

}  // namespace ss
