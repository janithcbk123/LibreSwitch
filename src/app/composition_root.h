// =====================================================================
//  app/composition_root.h — on-device composition root (ON-DEVICE)
// ---------------------------------------------------------------------
//  THE one place where abstract seams meet concrete LibreTiny/FlashDB/
//  FreeRTOS implementations. Everything else in the codebase is pure and
//  host-tested; this file is deliberately the only "impure" assembly
//  point. It:
//    1. holds every component in STATIC storage (no heap — Phase 3),
//    2. resolves the device profile (from KVS; safe-fault if unknown),
//    3. brings up the local adapters (relay/touch/indicator/persistence),
//    4. applies the boot restore policy (R-7 safe default),
//    5. launches the Control task running the ControlLoop.
//
//  Compiled on-device only. Its correctness is proven by on-target
//  bring-up (the R-0-class boundary), NOT host tests — but every PIECE
//  it wires is already host-tested, so bring-up is about the physical
//  truths (does the pin click, does FlashDB bind), not logic.
//
//  Connectivity is fully wired here too — provisioning (Phase 7, always
//  runs: stored-creds STA connect or SoftAP portal), MQTT (Phase 6,
//  enabled when broker config + TLS material exist in KVS), OTA (Phase
//  8, manifest via cmd/system/ota → verify-before-commit → R-24 health
//  confirm on next boot), SNTP (feeds the Phase 9 dual clock), and the
//  factory-reset action (wipe creds, KEEP profile_id — Q1). None of it
//  can block local control: it all lives in the net task.
// =====================================================================
#pragma once

#if defined(LT_BUILD) || defined(ARDUINO)

#include <optional>
#include <Arduino.h>          // Serial1 + lt_get_device_mac (via libretiny.h)
#include "control_loop.h"
#include "command_sink.h"
#include "fan_out_state_change.h"
#include "../platform/gpio_hal_libretiny.h"
#include "../platform/kv_store_flashdb.h"
#include "../platform/scheduler_freertos.h"
#include "../platform/clock.h"
#include "../platform/critical_section.h"
#include "../platform/sync_queue.h"
#include "../adapters/gpio_relay_sink.h"
#include "../adapters/gpio_touch_source.h"
#include "../adapters/gpio_indicator_sink.h"
#include "../adapters/flashdb_state_persistence.h"
#include "../domain/relay_controller.h"
#include "../domain/control_coordinator.h"
#include "../domain/indicator_controller.h"
#include "../domain/boot_policy.h"
#include "../profiles/profile_resolver.h"
#include "../net/mqtt_service.h"
#include "../net/mqtt_client_pubsub.h"
#include "../net/provisioning_service.h"
#include "../net/provisioning_impl.h"
#include "../net/ota_service.h"
#include "../net/ota_impl.h"
#include "../net/ota_manifest.h"
#include "../net/ota_health_confirm.h"
#include "../net/net_time.h"
#include "../log/logger.h"
#include "../log/log_impl.h"
#include "logging_state_listener.h"
#include "persisting_state_listener.h"
#include "serial_state_listener.h"

// Current firmware version (Phase 8 anti-downgrade reference). Override
// per release:  -D FW_VER_MAJOR=1 -D FW_VER_MINOR=2 -D FW_VER_PATCH=3
#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR 0
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR 1
#endif
#ifndef FW_VER_PATCH
#define FW_VER_PATCH 0
#endif

namespace ss {

// Minimal factory-reset handler for the local plane. (Full reset wipes
// creds + reprovisions — that lands with the connectivity layer; here we
// at least surface the gesture and stage indicator feedback.)
class LocalResetHandler final : public FactoryResetHandler {
public:
    explicit LocalResetHandler(IndicatorController& ind) : ind_(ind) {}
    void onFactoryResetRequested() override { resetRequested_ = true; }
    void onFactoryResetProgress(Millis e, Millis t) override {
        ind_.setStatus(DeviceStatus::FactoryReset);
        ind_.setResetProgress(e, t);
    }
    bool consumeResetRequest() { bool r = resetRequested_; resetRequested_ = false; return r; }
private:
    IndicatorController& ind_;
    bool resetRequested_ = false;
};

// All components live here in static storage — constructed once, never
// freed (Phase 3: static, no heap churn). A single instance (s_app).
class CompositionRoot {
public:
    // Boot sequence. Returns false if a fatal bring-up step fails (caller
    // can signal a fault pattern on the LED and halt safely).
    bool begin() {
        Serial1.println("[cr] begin: binding KVS partition 'kvs'...");
        if (!kv_.begin("kvs")) {
            Serial1.println("[cr] FAIL: fdb_kvdb_init('kvs') returned error "
                            "(partition missing/unformatted/misnamed)");
            return false;                                // FlashDB KVS bind
        }
        Serial1.println("[cr] KVS bound OK");

        // Logging up first so boot/bring-up events are captured (Phase 9).
        if (logSink_.begin("userdata")) {
            logger_.emplace(logSink_, timeSource_, kv_);
            logger_->begin();                            // boot event + boot_id
            logListener_.emplace(*logger_);
            Serial1.println("[cr] log TSDB bound on 'userdata'");
        } else {
            Serial1.println("[cr] WARN: log bind on 'userdata' failed (non-fatal)");
        }

        Serial1.println("[cr] resolving device profile from KVS 'profile_id'...");
        if (!resolveProfile()) {
            Serial1.println("[cr] FAIL: no valid 'profile_id' in KVS "
                            "(fresh device? pre-seed it, e.g. 4 for switch_4g)");
            if (logger_) logger_->log(EventCode::FlashError, Severity::Critical);
            return false;                                // unknown profile = fault
        }
        Serial1.print("[cr] profile resolved, id=");
        Serial1.println(static_cast<int>(profile_->id));
        if (logger_) logger_->log(EventCode::ProfileLoaded, Severity::Info,
                                  static_cast<uint16_t>(profile_->id));

        // Relay polarity. Default ActiveHigh (pin HIGH = relay ON), which
        // matched correct touch behaviour on this board. Override without
        // editing code via:  -D RELAY_ACTIVE_LOW   (if a variant is wired
        // active-low). Pins themselves live in the device profile and can
        // be overridden with -D RELAY_PIN_0..3 (see profile_registry.h).
#if defined(RELAY_ACTIVE_LOW)
        relaySink_.emplace(hal_, *profile_, RelayPolarity::ActiveLow);
        Serial1.println("[cr] relay polarity = ActiveLow (pin LOW = relay ON)");
#else
        relaySink_.emplace(hal_, *profile_, RelayPolarity::ActiveHigh);
        Serial1.println("[cr] relay polarity = ActiveHigh (pin HIGH = relay ON)");
#endif
        relays_.emplace(*relaySink_);
        indSink_.emplace(hal_, *profile_);
        indicator_.emplace(*indSink_);
        resetHandler_.emplace(*indicator_);
        // coordinator announces through the fan-out (MQTT + future logger)
        coord_.emplace(*relays_, fanOut_, *resetHandler_);
        touchEngine_.emplace(*coord_, touchCfg_);
#if defined(TOUCH_ACTIVE_LOW)
        touchSrc_.emplace(hal_, *profile_, *touchEngine_, TouchActiveLevel::ActiveLow);
        Serial1.println("[cr] touch active level = ActiveLow (pad idle HIGH)");
#else
        touchSrc_.emplace(hal_, *profile_, *touchEngine_, TouchActiveLevel::ActiveHigh);
        Serial1.println("[cr] touch active level = ActiveHigh (pad idle LOW)");
#endif
        persistence_.emplace(kv_);
        // command queue is cross-task (Mqtt → Control): use the synchronized
        // queue + a drain adapter for the loop and a port for producers.
        cmdQueue_.emplace(cs_, OverflowPolicy::RejectWhenFull);
        cmdDrain_.emplace(*cmdQueue_);
        cmdPort_.emplace(*cmdQueue_);
        loop_.emplace(*touchSrc_, *coord_, *indicator_, *cmdDrain_);

        // relay changes fan out to BOTH the logger and (later) MQTT.
        if (logListener_) fanOut_.add(&*logListener_);
        fanOut_.add(&serialListener_);          // print every change to UART

        if (!bringUpAdapters()) return false;
        applyBootState();

        // Register the persistence listener AFTER restoring boot state (so
        // restore itself doesn't trigger a redundant save). From here on,
        // every relay change is written to flash for the next boot.
        persistListener_.emplace(*relays_, *persistence_);
        fanOut_.add(&*persistListener_);

        bringUpMqtt();                                   // best-effort; never blocks local
        bringUpNet();                                    // provisioning stack (always)

        // R-24: a freshly-committed OTA image must confirm health within
        // the window or be rolled back. The flag was set pre-reboot by
        // OtaService::markAwaitingConfirm.
        uint8_t pend = 0;
        if (kv_.getBlob("ota_pending", &pend, 1) == 1 && pend == 1) {
            otaPending_ = true;
            health_.onBoot(clock_.nowMs());
            Serial1.println("[ota] post-OTA boot: health-confirm window armed (180s)");
        }

        indicator_->setStatus(DeviceStatus::Offline);    // local-only until connected
        Serial1.print("[cr] fw version ");
        Serial1.print(FW_VER_MAJOR); Serial1.print('.');
        Serial1.print(FW_VER_MINOR); Serial1.print('.');
        Serial1.println(FW_VER_PATCH);
        return true;
    }

    // Called by the Control task each cycle.
    void controlTick() {
        loop_->tick(clock_.nowMs());
        if (otaPending_) health_.markLocalControlOk();   // R-24 criterion
        if (resetHandler_->consumeResetRequest()) factoryReset();
    }

    // Called by the Net task each cycle: drives provisioning (portal or
    // stored-creds STA), then — once Wi-Fi is Operational — SNTP, MQTT,
    // and the post-OTA health window. Local control never depends on it.
    void netTick() {
        const Millis now = clock_.nowMs();
        if (!prov_) return;

        if (!provBegun_) {
            provBegun_ = true;
            const ProvState st = prov_->begin(now);
            if (st == ProvState::ApPortal) {
                indicator_->setStatus(DeviceStatus::Provisioning);
                Serial1.print("[net] provisioning portal up: SSID=");
                Serial1.print(apSsid_);
                Serial1.print(" pass=");
                Serial1.print(apPass_);
                Serial1.println("  → http://192.168.4.1");
            }
        }
        prov_->tick(now);

        const bool operational = (prov_->state() == ProvState::Operational);
        if (operational && !wifiOperational_) {
            wifiOperational_ = true;
            startSntpOnce();                              // Phase 9 UTC clock
            indicator_->setStatus(mqtt_ ? DeviceStatus::Connecting
                                        : DeviceStatus::Offline);
            Serial1.println("[net] wifi operational; SNTP started");
        }
        if (!operational) return;

        if (mqtt_) {
            const bool was = mqtt_->isConnected();
            mqtt_->tick(now, /*jitterSeed*/ now, /*tsForPublish*/ now / 1000);
            const bool is = mqtt_->isConnected();
            if (is && !was) indicator_->setStatus(DeviceStatus::Online);
            if (!is && was) indicator_->setStatus(DeviceStatus::Offline);
            if (otaPending_ && is) health_.markNetworkUp();   // R-24 criterion
        }
        if (otaPending_) healthTick(now);
    }

    // ---- ONE-TIME BRING-UP SEED ----
    // Writes the profile_id byte using the member KVS (its FlashDB control
    // block lives in .bss, not on setup()'s small stack — unlike a local
    // fdb_kvdb, which can overflow the init stack). Returns true on success.
    bool seedProfileId(uint8_t id) {
        if (!kv_.begin("kvs")) return false;       // same path as begin()
        return kv_.setBlob("profile_id", &id, sizeof(id));
    }

    Scheduler& scheduler() { return sched_; }
    bool mqttEnabled() const { return mqtt_.has_value(); }

private:
    bool resolveProfile() {
        uint8_t raw = 0;
        // profile byte persisted at manufacture/provisioning (Q1).
        const size_t n = kv_.getBlob("profile_id", &raw, sizeof(raw));
        ProfileLookup lk = (n == sizeof(raw)) ? ProfileResolver::resolveRaw(raw)
                                              : ProfileLookup{nullptr, Error::NotInitialized};
        if (!lk.ok()) return false;     // unknown/missing profile = fault, never guess
        profile_ = lk.profile;
        return true;
    }

    bool bringUpAdapters() {
        if (!relaySink_->begin().ok()) return false;
        if (!touchSrc_->begin().ok()) return false;
        if (!indSink_->begin().ok())  return false;
        if (!relays_->init(profile_->channelCount).ok()) return false;
        if (!touchEngine_->init(profile_->channelCount).ok()) return false;
        return true;
    }

    void applyBootState() {
        // RestoreLast with R-7 safe fallback baked into BootPolicy.
        const PersistedRelayState probe = persistence_->loadRelayState();
        Serial1.print("[cr] boot state: persisted=");
        Serial1.println(probe.valid ? "FOUND (restoring last)" : "absent (safe all-OFF)");
        BootPolicy boot(*relays_, *persistence_);
        boot.applyBootState(RestorePolicy::RestoreLast);
        Serial1.print("[cr] relays after boot: ");
        for (ChannelId c = 0; c < profile_->channelCount; ++c) {
            Serial1.print(relays_->state(c) == RelayState::On ? "1" : "0");
        }
        Serial1.println();
    }

    // Best-effort MQTT bring-up. If config/creds are absent (e.g. not yet
    // provisioned), MQTT simply stays disabled — local control is wholly
    // unaffected (Phase 1). Returns nothing; failure is non-fatal.
    void bringUpMqtt() {
        // Broker host + topic root + device id come from KVS (config-driven,
        // Phase 6 D6-2). Absent any of them → skip MQTT (not provisioned).
        if (kv_.getBlob("mqtt_host", brokerHost_, sizeof(brokerHost_)) == 0) return;
        if (kv_.getBlob("mqtt_root", topicRoot_, sizeof(topicRoot_)) == 0)
            snprintf(topicRoot_, sizeof(topicRoot_), "sw");      // sensible default
        if (kv_.getBlob("device_id", deviceId_, sizeof(deviceId_)) == 0) return;

        // TLS material (operational cert/key + CA) from KVS (Phase 5). If
        // missing, skip — mTLS is mandatory, no plaintext fallback.
        if (kv_.getBlob("tls_ca",   tlsCa_,   sizeof(tlsCa_))   == 0) return;
        if (kv_.getBlob("tls_cert", tlsCert_, sizeof(tlsCert_)) == 0) return;
        if (kv_.getBlob("tls_key",  tlsKey_,  sizeof(tlsKey_))  == 0) return;

        pubsub_.emplace();
        pubsub_->setTls(tlsCa_, tlsCert_, tlsKey_);
        snprintf(clientId_, sizeof(clientId_), "%s", deviceId_);
        mqtt_.emplace(*pubsub_, *cmdPort_, topicRoot_, deviceId_, clientId_);
        mqtt_->configure();
        mqtt_->setServer(brokerHost_, 8883);             // mTLS port
        fanOut_.add(&*mqtt_);                             // relay changes → publish

        // OTA arrives via MQTT (cmd/system/ota) → wire the Phase 8 stack.
        mqtt_->setSystemSink(sysSink_);
        otaVerifier_.emplace(otaWriter_);                 // DEV integrity (R-23 note)
        otaDownloader_.emplace(tlsCa_);                   // CA for https URLs
        const FwVersion cur{ FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH };
        ota_.emplace(otaWriter_, *otaVerifier_, *otaDownloader_, reboot_,
                     kv_, *profile_, cur, otaProgress_);
    }

    // Provisioning stack (Phase 7). Always constructed: at boot it either
    // connects with stored creds (→ Operational) or opens the SoftAP
    // portal. Per-device SSID/pass derived from the MAC (R-20), printed
    // on UART at portal-open so the installer can join.
    void bringUpNet() {
        uint8_t mac[6] = {};
        lt_get_device_mac(mac);
        snprintf(apSsid_, sizeof(apSsid_), "Switch-%02X%02X", mac[4], mac[5]);
        snprintf(apPass_, sizeof(apPass_), "sw-%02X%02X%02X", mac[3], mac[4], mac[5]);
        enroll_.emplace(kv_);                        // R-19 dev enrollment
        ProvConfig cfg{ apSsid_, apPass_ };
        prov_.emplace(wifiCtl_, portal_, *enroll_, kv_, cfg);
    }

    // cmd/system/ota → parse manifest → run the full Phase 8 pipeline.
    // Runs in the Net task (download blocks it; Control is unaffected).
    void handleOtaManifest(const uint8_t* payload, size_t len) {
        if (!ota_) { Serial1.println("[ota] manifest received but OTA stack absent"); return; }
        const OtaManifest m = OtaManifestCodec::parse(payload, len);
        if (!m.valid) { Serial1.println("[ota] BAD manifest (required: url,ver,sig,hw,ch,size)"); return; }
        Serial1.print("[ota] manifest ok: v");
        Serial1.print(m.version.major); Serial1.print('.');
        Serial1.print(m.version.minor); Serial1.print('.');
        Serial1.print(m.version.patch);
        Serial1.print(" size="); Serial1.println(m.sizeBytes);
        if (mqtt_) mqtt_->yieldConnection();         // exclusive-TLS (Phase 3)
        const OtaResult r = ota_->apply(m);          // verify-before-commit inside
        Serial1.print("[ota] result code=");
        Serial1.println(static_cast<int>(r));        // Ok==0 never returns (reboot)
    }

    // R-24 health window: all criteria within 180 s or roll back.
    void healthTick(Millis now) {
        const HealthOutcome h = health_.evaluate(now);
        if (h == HealthOutcome::Confirmed) {
            kv_.erase("ota_pending"); kv_.erase("ota_pending_ver");
            otaPending_ = false;
            Serial1.println("[ota] health CONFIRMED — image accepted");
        } else if (h == HealthOutcome::Rollback) {
            Serial1.println("[ota] health window expired — ROLLING BACK");
            kv_.erase("ota_pending"); kv_.erase("ota_pending_ver");
            otaWriter_.rollBack();                   // revert to previous image
            reboot_.reboot();
        }
    }

    // Factory reset (Q4 gesture): wipe creds + state, KEEP profile_id
    // (Q1: the hardware profile survives factory reset), then reboot
    // into the provisioning portal.
    void factoryReset() {
        Serial1.println("[reset] FACTORY RESET: wiping creds (profile kept)");
        static const char* kWipe[] = {
            "sta_ssid", "sta_pass", "enroll_token",
            "mqtt_host", "mqtt_root", "device_id",
            "tls_ca", "tls_cert", "tls_key",
            "relay_state", "ota_pending", "ota_pending_ver",
        };
        for (const char* k : kWipe) kv_.erase(k);
        reboot_.reboot();
    }

    // MQTT system-command glue (manifest + explicit health confirm).
    struct SystemSink final : MqttService::SystemCommandSink {
        explicit SystemSink(CompositionRoot& r) : r_(r) {}
        void onOtaManifest(const uint8_t* p, size_t n) override { r_.handleOtaManifest(p, n); }
        void onOtaConfirm() override {
            if (r_.otaPending_) {
                r_.health_.markExplicitConfirm();    // R-24 criterion
                Serial1.println("[ota] explicit confirm received");
            }
        }
        CompositionRoot& r_;
    };

    // OTA progress → UART (and a place to hang MQTT resp publishing later).
    struct SerialOtaProgress final : OtaProgress {
        void onPhase(OtaPhase phase, OtaResult result) override {
            Serial1.print("[ota] phase=");
            Serial1.print(static_cast<int>(phase));
            Serial1.print(" result=");
            Serial1.println(static_cast<int>(result));
        }
    };

    // --- concrete platform impls (the only LibreTiny/FlashDB instances) ---
    LibreTinyGpioHal hal_;
    FlashDbKvStore   kv_;
    FreeRtosScheduler sched_;
    ArduinoClock     clock_;

    const DeviceProfile* profile_ = nullptr;
    TouchConfig          touchCfg_{};
    FanOutStateChange<5> fanOut_;            // coordinator → MQTT + log + persist + serial
    SerialStateListener  serialListener_;    // prints every relay change to UART
    FreeRtosCriticalSection cs_;             // guards cross-task queues
    FlashDbTsdbSink         logSink_;        // Phase 9 event log
    SystemTimeSource        timeSource_;     // dual-clock (uptime + SNTP)

    // config/creds buffers (filled from KVS in bringUpMqtt)
    char brokerHost_[64] = {};
    char topicRoot_[24]  = {};
    char deviceId_[40]   = {};
    char clientId_[40]   = {};
    char tlsCa_[1800]    = {};
    char tlsCert_[1800]  = {};
    char tlsKey_[1800]   = {};

    // Components constructed in begin() once the profile is known. Using
    // optional<> for deferred construction without heap (in-place storage).
    template <class T> using slot = std::optional<T>;
    slot<GpioRelaySink>            relaySink_;
    slot<RelayController>          relays_;
    slot<GpioIndicatorSink>        indSink_;
    slot<IndicatorController>      indicator_;
    slot<LocalResetHandler>        resetHandler_;
    slot<ControlCoordinator>       coord_;
    slot<TouchInput>               touchEngine_;
    slot<GpioTouchSource>          touchSrc_;
    slot<FlashDbStatePersistence>  persistence_;
    slot<SyncQueue<ControlCommand, kCommandQueueDepth>> cmdQueue_;
    slot<SyncCommandSource<kCommandQueueDepth>>         cmdDrain_;
    slot<CommandQueuePort>         cmdPort_;
    slot<ControlLoop>              loop_;
    slot<PubSubMqttClient>         pubsub_;
    slot<MqttService>              mqtt_;
    slot<Logger>                   logger_;
    slot<LoggingStateListener>     logListener_;
    slot<PersistingStateListener>  persistListener_;

    // --- provisioning (Phase 7) ---
    LibreTinyWifiControl   wifiCtl_;
    LibreTinyPortalServer  portal_;
    slot<KvsDevEnrollment> enroll_;
    slot<ProvisioningService> prov_;
    char apSsid_[24] = {};
    char apPass_[24] = {};
    bool provBegun_ = false;
    bool wifiOperational_ = false;

    // --- OTA (Phase 8) + R-24 health confirm ---
    LibreTinyFirmwareWriter otaWriter_;
    LibreTinyReboot         reboot_;
    SerialOtaProgress       otaProgress_;
    slot<DevSha256Verifier> otaVerifier_;
    slot<HttpDownloader>    otaDownloader_;
    slot<OtaService>        ota_;
    SystemSink              sysSink_{*this};
    OtaHealthConfirm        health_;
    bool                    otaPending_ = false;
};

}  // namespace ss

#endif  // LT_BUILD || ARDUINO
