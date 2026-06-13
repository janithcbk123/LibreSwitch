// =====================================================================
//  src/main.cpp — firmware entry point (ON-DEVICE)
// ---------------------------------------------------------------------
//  LibreTiny calls setup() once then loop() forever, both inside a
//  FreeRTOS mainTask the platform creates (scheduler already running).
//  We use setup() to boot the composition root and spawn the Control
//  task (Phase 3 priority 5). loop() then just idles — all real work
//  happens in the Control task (and later the connectivity tasks).
//
//  Serial1 + the logger note from R-0: the bench UART is Serial1 with
//  LT_UART_DEFAULT_LOGGER=1 (default Serial maps to UART2). Kept here so
//  bring-up diagnostics land on the bench terminal.
// =====================================================================
#include <Arduino.h>
#include "app/composition_root.h"
#include "profiles/profile_registry.h"   // RELAY_PIN_x for early safe-park

using namespace ss;

// Single static instance — all firmware state lives here (no heap).
static CompositionRoot s_app;

// ---- ONE-TIME BRING-UP SEED (opt-in) ------------------------------------
// A fresh device has no 'profile_id' in KVS, so begin() correctly faults
// rather than guessing the hardware. To seed it during bring-up, build
// once with: -D SEED_PROFILE_ID=4   (4 = switch_4g; use your gang count).
// Flash, let it run once (it writes the byte then continues to normal
// boot), then REMOVE the flag and reflash so it doesn't re-seed every boot.
//
// NOTE: seeding goes through s_app.seedProfileId(), which uses the static
// composition root's member KVS (FlashDB control block in .bss). Do NOT
// allocate a local `struct fdb_kvdb` in setup() — it overflows the small
// init stack and hangs before any output (learned on-device).
// -------------------------------------------------------------------------

// -------------------------------------------------------------------------

// Control task body (Phase 3: Control task, priority 5, owns relay
// writes). Runs the ControlLoop at a fixed cadence. ~10 ms gives crisp
// touch response well under the 50 ms local-control budget (Phase 1).
static void controlTask(void* /*arg*/) {
    for (;;) {
        s_app.controlTick();
        s_app.scheduler().delayMs(10);
    }
}

// Net task body (Phase 3: priority 2). Always runs: drives provisioning
// (SoftAP portal or stored-creds STA), then SNTP + MQTT + the post-OTA
// health window once Wi-Fi is up. Best-effort by design — it can never
// compete with or block local control.
static void netTask(void* /*arg*/) {
    for (;;) {
        s_app.netTick();
        s_app.scheduler().delayMs(50);
    }
}

// Drive every relay control pin to its OFF level as the VERY FIRST thing
// after reset, before FlashDB/profile bring-up (~400 ms). This shrinks the
// boot relay-flash window to near-zero of firmware time. The remaining
// flash (bootloader-phase, pins floating) is a hardware property — the
// proper fix there is a pull resistor per relay line holding the
// de-energized level (see README known-issues).
static void parkRelaysSafe() {
#if defined(RELAY_ACTIVE_LOW)
    const uint8_t off = HIGH;           // active-low: HIGH = de-energized
#else
    const uint8_t off = LOW;            // active-high: LOW = de-energized
#endif
    const uint8_t pins[] = { RELAY_PIN_0, RELAY_PIN_1, RELAY_PIN_2, RELAY_PIN_3 };
    for (uint8_t p : pins) { pinMode(p, OUTPUT); digitalWrite(p, off); }
}

void setup() {
    parkRelaysSafe();                   // FIRST: relays to safe-OFF
    Serial1.begin(115200);              // bench diagnostics (R-0 UART finding)
    Serial1.println("[boot] smartswitch starting");

#if defined(SEED_PROFILE_ID)
    {
        const bool ok = s_app.seedProfileId((uint8_t)(SEED_PROFILE_ID));
        Serial1.print("[seed] profile_id=");
        Serial1.print((int)(SEED_PROFILE_ID));
        Serial1.println(ok ? " written OK (remove -D SEED_PROFILE_ID and reflash)"
                           : " WRITE FAILED");
    }
#else
    Serial1.println("[seed] SEED_PROFILE_ID not set in this build "
                    "(add -D SEED_PROFILE_ID=4 to platformio.ini build_flags to seed)");
#endif

    if (!s_app.begin()) {
        // Fatal bring-up failure (e.g. unknown profile, FlashDB bind
        // failed). Halt safely — relays were driven OFF at adapter begin()
        // before any failure point, so the device is in a safe state.
        Serial1.println("[boot] FATAL: composition root begin() failed");
        for (;;) { /* safe halt; watchdog/UART recovery from here */ }
    }

    TaskSpec control{ "control", controlTask, nullptr,
                      /*stackWords*/ 2048, /*priority*/ 5 };
    if (!s_app.scheduler().createTask(control)) {
        Serial1.println("[boot] FATAL: control task create failed");
        for (;;) {}
    }

    // Net task always runs (provisioning portal or STA + MQTT + OTA). The
    // device is fully functional locally without it (Phase 1).
    TaskSpec net{ "net", netTask, nullptr, /*stackWords*/ 4096, /*priority*/ 2 };
    if (!s_app.scheduler().createTask(net))
        Serial1.println("[boot] WARN: net task create failed (local control unaffected)");
    else
        Serial1.println(s_app.mqttEnabled()
                        ? "[boot] net task started (mqtt configured)"
                        : "[boot] net task started (unprovisioned → portal)");

    Serial1.println("[boot] control plane up");
}

void loop() {
    // All work is in the Control task; idle the platform main loop.
    delay(1000);
}
