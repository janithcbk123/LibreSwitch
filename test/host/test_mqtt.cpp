// =====================================================================
//  test/host/test_mqtt.cpp — L1 unit tests for the MQTT adapter
// ---------------------------------------------------------------------
//  Topic building, payload codec (incl. malformed rejection), backoff
//  timing/reset, inbound command routing through ControlCommandPort
//  (the "MQTT never writes a relay directly" path), outbound state
//  publish + queue drop-oldest, and connect-with-LWT lifecycle — all
//  against a mock MqttClient + mock ControlCommandPort.
// =====================================================================
#include "test_framework.h"
#include "../../src/net/mqtt_service.h"
#include <string>
#include <vector>
#include <cstring>

using namespace ss;

// ---------------- MqttTopics ----------------
TEST(topic_cmd_wildcard) {
    MqttTopics t("sw", "dev01");
    char b[kMaxTopicLen]; CHECK(t.cmdWildcard(b, sizeof(b)));
    EQ(std::string(b), std::string("sw/dev01/cmd/#"));
}
TEST(topic_evt_relay_state) {
    MqttTopics t("home", "switch-AB12");
    char b[kMaxTopicLen]; t.evtRelayState(b, sizeof(b), 2);
    EQ(std::string(b), std::string("home/switch-AB12/evt/relay/2/state"));
}
TEST(topic_truncation_returns_false) {
    MqttTopics t("sw", "dev01");
    char b[8]; CHECK(!t.evtRelayState(b, sizeof(b), 1));   // too small
}
TEST(parse_relay_set_channel) {
    ChannelId ch = 0xFF;
    CHECK(MqttTopics::parseRelaySetChannel("sw/dev01/cmd/relay/3/set", ch));
    EQ(ch, (ChannelId)3);
}
TEST(parse_relay_set_rejects_all_and_malformed) {
    ChannelId ch;
    CHECK(!MqttTopics::parseRelaySetChannel("sw/dev01/cmd/relay/all/set", ch));
    CHECK(!MqttTopics::parseRelaySetChannel("sw/dev01/cmd/relay//set", ch));
    CHECK(!MqttTopics::parseRelaySetChannel("sw/dev01/evt/relay/1/state", ch));
    CHECK(MqttTopics::isRelayAllSet("sw/dev01/cmd/relay/all/set"));
}

// ---------------- MqttPayload ----------------
TEST(payload_parse_on_true_false) {
    bool on;
    CHECK(MqttPayload::parseRelaySet((const uint8_t*)"{\"on\":true}", 11, on)); CHECK(on);
    CHECK(MqttPayload::parseRelaySet((const uint8_t*)"{\"on\":false}", 12, on)); CHECK(!on);
    CHECK(MqttPayload::parseRelaySet((const uint8_t*)"{ \"on\" : true }", 15, on)); CHECK(on);
}
TEST(payload_parse_rejects_malformed) {
    bool on;
    CHECK(!MqttPayload::parseRelaySet((const uint8_t*)"{\"x\":true}", 10, on));
    CHECK(!MqttPayload::parseRelaySet((const uint8_t*)"garbage", 7, on));
    CHECK(!MqttPayload::parseRelaySet((const uint8_t*)"", 0, on));
    CHECK(!MqttPayload::parseRelaySet((const uint8_t*)"{\"on\":maybe}", 12, on));
}
TEST(payload_serialize_relay_state) {
    char b[64];
    size_t n = MqttPayload::relayState(b, sizeof(b), RelayState::On, CommandSource::Local, 12345);
    CHECK(n > 0);
    EQ(std::string(b), std::string("{\"on\":true,\"src\":\"local\",\"ts\":12345}"));
}
TEST(payload_serialize_online) {
    char b[32];
    MqttPayload::online(b, sizeof(b), false);
    EQ(std::string(b), std::string("{\"online\":false}"));
}

// ---------------- ReconnectBackoff ----------------
TEST(backoff_ready_before_first_failure) {
    ReconnectBackoff b;
    CHECK(b.ready(0));                        // not armed → may connect
}
TEST(backoff_blocks_then_allows_after_window) {
    ReconnectBackoff b(1000, 60000, 0);       // no jitter for determinism
    b.onFailure(0, 0);
    CHECK(!b.ready(500));                      // still backing off (1000ms)
    CHECK(b.ready(1000));                      // window elapsed
}
TEST(backoff_doubles_and_caps) {
    ReconnectBackoff b(1000, 4000, 0);
    EQ(b.currentDelay(), (Millis)1000);
    b.onFailure(0, 0); EQ(b.currentDelay(), (Millis)2000);
    b.onFailure(0, 0); EQ(b.currentDelay(), (Millis)4000);
    b.onFailure(0, 0); EQ(b.currentDelay(), (Millis)4000);   // capped
}
TEST(backoff_resets_on_success) {
    ReconnectBackoff b(1000, 60000, 0);
    b.onFailure(0, 0); b.onFailure(0, 0);
    b.onSuccess();
    EQ(b.currentDelay(), (Millis)1000);
    CHECK(b.ready(0));
}
TEST(backoff_jitter_stays_in_band) {
    ReconnectBackoff b(1000, 60000, 20);      // +/-20%
    // try several seeds; next delay must land within [800,1200] before doubling
    for (uint32_t seed = 0; seed < 50; ++seed) {
        ReconnectBackoff x(1000, 60000, 20);
        x.onFailure(0, seed);
        // window is now+delay; check the scheduled delay by readiness edges
        // (delay = nextAttempt - now). We can't read it directly, so assert
        // not-ready just before 800 and ready by 1200.
        CHECK(!x.ready(799));
        CHECK(x.ready(1200));
    }
}

// ---------------- MqttService routing + lifecycle ----------------
struct MockMqtt : MqttClient {
    bool conn = false;
    int  connectCalls = 0;
    std::vector<std::string> published;        // "topic|payload|retain"
    std::vector<std::string> subscribed;
    MqttInboundHandler* handler = nullptr;
    std::string lwtTopicCopy;          // durable copy (params borrow transient bufs)
    bool lastClean = false, lastRetain = false; uint8_t lastQos = 0;
    bool nextConnectResult = true;

    void configure(uint16_t, uint16_t, MqttInboundHandler& h) override { handler = &h; }
    void setServer(const char*, uint16_t) override {}
    bool connect(const MqttConnectParams& p) override {
        ++connectCalls;
        // PubSubClient consumes these synchronously during connect(); the
        // service builds them in locals, so we must COPY what we want to
        // assert (mirrors how the real client uses them in-call).
        lwtTopicCopy = p.lwtTopic ? p.lwtTopic : "";
        lastClean = p.cleanSession; lastRetain = p.lwtRetain; lastQos = p.lwtQos;
        conn = nextConnectResult; return conn;
    }
    bool publish(const char* t, const uint8_t* p, size_t n, bool r) override {
        published.push_back(std::string(t) + "|" + std::string((const char*)p, n) + "|" + (r?"R":"-"));
        return true;
    }
    bool subscribe(const char* t, uint8_t) override { subscribed.push_back(t); return true; }
    bool loop() override { return true; }
    bool connected() override { return conn; }
    void disconnect() override { conn = false; }
    int  lastState() override { return conn ? 0 : -1; }
    // test helper: simulate an inbound broker message
    void inject(const char* topic, const char* payload) {
        if (handler) handler->onMessage(topic, (const uint8_t*)payload, strlen(payload));
    }
};

struct MockCmdPort : ControlCommandPort {
    std::vector<ControlCommand> cmds;
    Status submit(const ControlCommand& c) override { cmds.push_back(c); return Status::success(); }
};

struct SvcRig {
    MockMqtt mqtt; MockCmdPort port;
    MqttService svc{mqtt, port, "sw", "dev01", "client-dev01"};
    SvcRig() { svc.configure(); }
};

TEST(service_connect_publishes_online_and_subscribes) {
    SvcRig r;
    r.svc.tick(0, 0, 100);                     // not connected → attempt
    EQ(r.mqtt.connectCalls, 1);
    CHECK(r.mqtt.conn);
    // online retained publish + cmd/# subscribe
    bool sawOnline = false;
    for (auto& p : r.mqtt.published) if (p.find("evt/system/online") != std::string::npos && p.find("true")!=std::string::npos) sawOnline = true;
    CHECK(sawOnline);
    EQ(r.mqtt.subscribed.size(), (size_t)1);
    EQ(r.mqtt.subscribed[0], std::string("sw/dev01/cmd/#"));
}

TEST(service_connect_sets_lwt_and_clean_session) {
    SvcRig r;
    r.svc.tick(0, 0, 0);
    CHECK(r.mqtt.lastClean);                     // Phase 6 D6-4 clean session
    CHECK(r.mqtt.lastRetain);
    EQ(r.mqtt.lastQos, (uint8_t)1);
    CHECK(r.mqtt.lwtTopicCopy.find("evt/system/online") != std::string::npos);
}

// THE critical test: inbound command routes through the port, NOT a relay.
TEST(inbound_relay_set_routes_through_command_port) {
    SvcRig r;
    r.svc.tick(0, 0, 0);                        // connect
    r.mqtt.inject("sw/dev01/cmd/relay/2/set", "{\"on\":true}");
    EQ(r.port.cmds.size(), (size_t)1);
    EQ(r.port.cmds[0].kind, CommandKind::SetChannel);
    EQ(r.port.cmds[0].channel, (ChannelId)2);
    EQ(r.port.cmds[0].state, RelayState::On);
    EQ(r.port.cmds[0].source, CommandSource::Cloud);   // attributed
}

TEST(inbound_all_set_routes_setall) {
    SvcRig r; r.svc.tick(0,0,0);
    r.mqtt.inject("sw/dev01/cmd/relay/all/set", "{\"on\":false}");
    EQ(r.port.cmds.size(), (size_t)1);
    EQ(r.port.cmds[0].kind, CommandKind::SetAll);
    EQ(r.port.cmds[0].state, RelayState::Off);
}

TEST(inbound_malformed_payload_is_ignored) {
    SvcRig r; r.svc.tick(0,0,0);
    r.mqtt.inject("sw/dev01/cmd/relay/0/set", "not json");
    CHECK(r.port.cmds.empty());                 // defensive: ignored
}

TEST(inbound_unknown_topic_is_ignored) {
    SvcRig r; r.svc.tick(0,0,0);
    r.mqtt.inject("sw/dev01/evt/relay/0/state", "{\"on\":true}");
    CHECK(r.port.cmds.empty());
}

TEST(outbound_state_change_publishes_when_connected) {
    SvcRig r; r.svc.tick(0,0,0);
    r.mqtt.published.clear();
    r.svc.onStateChanged(1, RelayState::On, CommandSource::Local);  // queued
    r.svc.tick(0, 0, 777);                       // connected → drains
    bool saw = false;
    for (auto& p : r.mqtt.published)
        if (p.find("evt/relay/1/state")!=std::string::npos && p.find("\"ts\":777")!=std::string::npos) saw = true;
    CHECK(saw);
}

TEST(state_changes_queue_while_disconnected_then_flush) {
    SvcRig r;
    r.mqtt.nextConnectResult = false;            // stay disconnected
    r.svc.onStateChanged(0, RelayState::On, CommandSource::Local);
    r.svc.onStateChanged(1, RelayState::On, CommandSource::Cloud);
    r.svc.tick(0, 0, 0);                          // fails to connect; nothing published
    CHECK(r.mqtt.published.empty());
    // now allow connect; queued states flush on next connected tick
    r.mqtt.nextConnectResult = true;
    r.svc.tick(100000, 0, 5);                     // backoff elapsed → connect
    r.svc.tick(100001, 0, 5);                     // connected → drain
    bool ch0=false, ch1=false;
    for (auto& p : r.mqtt.published) {
        if (p.find("evt/relay/0/state")!=std::string::npos) ch0=true;
        if (p.find("evt/relay/1/state")!=std::string::npos) ch1=true;
    }
    CHECK(ch0); CHECK(ch1);
}

TEST(reconnect_uses_backoff_not_every_tick) {
    SvcRig r;
    r.mqtt.nextConnectResult = false;
    r.svc.tick(0, 0, 0);          EQ(r.mqtt.connectCalls, 1);   // first try
    r.svc.tick(10, 0, 0);         EQ(r.mqtt.connectCalls, 1);   // backing off
    r.svc.tick(2000, 0, 0);       EQ(r.mqtt.connectCalls, 2);   // window passed
}

TEST(payload_serialize_all_sources) {
    char b[64];
    struct { CommandSource s; const char* name; } cases[] = {
        {CommandSource::Local, "local"}, {CommandSource::Cloud, "cloud"},
        {CommandSource::Lan, "lan"}, {CommandSource::Boot, "boot"},
        {CommandSource::Internal, "internal"},
    };
    for (auto& c : cases) {
        MqttPayload::relayState(b, sizeof(b), RelayState::Off, c.s, 1);
        CHECK(std::string(b).find(std::string("\"src\":\"") + c.name + "\"") != std::string::npos);
    }
}


// ---- system command routing (new): cmd/system/ota|confirm → sink ----
TEST(routes_system_ota_and_confirm_to_sink) {
    struct Sink : MqttService::SystemCommandSink {
        int manifests = 0, confirms = 0; std::string last;
        void onOtaManifest(const uint8_t* p, size_t n) override {
            manifests++; last.assign((const char*)p, n);
        }
        void onOtaConfirm() override { confirms++; }
    } sink;
    SvcRig r;
    r.svc.setSystemSink(sink);
    const char* payload = "url=u;ver=1.0.1;sig=s;hw=h;ch=4;size=5";
    r.mqtt.inject("sw/dev01/cmd/system/ota", payload);
    r.mqtt.inject("sw/dev01/cmd/system/confirm", "1");
    EQ(sink.manifests, 1);
    EQ(sink.confirms, 1);
    CHECK(sink.last == payload);
    // system topics must NOT leak into the relay command path
    EQ((int)r.port.cmds.size(), 0);
}

TEST(system_topics_suffix_exact_no_substring_traps) {
    CHECK(MqttTopics::isSystemOta("sw/d/cmd/system/ota"));
    CHECK(!MqttTopics::isSystemOta("sw/d/cmd/system/ota-extra"));
    CHECK(!MqttTopics::isSystemOta("sw/d/cmd/system/confirm"));
    CHECK(MqttTopics::isSystemConfirm("sw/d/cmd/system/confirm"));
    CHECK(!MqttTopics::isSystemConfirm("sw/d/cmd/system/ota"));
}

TEST(system_commands_without_sink_are_ignored_safely) {
    SvcRig r;                            // no sink set
    r.mqtt.inject("sw/dev01/cmd/system/ota", "url=u;ver=1.0.0;sig=s;hw=h;ch=1;size=1");
    r.mqtt.inject("sw/dev01/cmd/system/confirm", "1");
    EQ((int)r.port.cmds.size(), 0);      // no crash, no relay leakage
}

int main() {
    printf("MQTT adapter unit tests\n");
    return tf::run_all();
}
