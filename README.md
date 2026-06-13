# LibreSwitch

**Open-source firmware for CB3S / BK7231N smart wall switches** — local-first,
device-authoritative, and built so local touch control *never* depends on the
network. A clean-architecture alternative firmware for Tuya-style 1–4 gang
switches, flashable with [LibreTiny](https://github.com/libretiny-eu/libretiny).

> **CB3S vs BK7231N:** the **CB3S** is the Tuya Wi-Fi module you'll see printed
> on the board; the **BK7231N** (Beken) is the MCU inside it. They refer to the
> same target — LibreTiny board `generic-bk7231n-qfn32-tuya`. Built and tested
> on a real CB3S-based 4-gang switch.


![host-tests](https://github.com/janithcbk123/LibreSwitch/actions/workflows/host-tests.yml/badge.svg)
![license](https://img.shields.io/badge/license-MIT-blue)
![platform](https://img.shields.io/badge/platform-LibreTiny%20%7C%20CB3S%20%2F%20BK7231N-green)

> ⚠️ **Status: works on real hardware for the local plane; not production-ready.**
> Boot, FlashDB storage, touch→relay control, and state persistence are proven
> on-device. Provisioning, MQTT/mTLS, and OTA are implemented and host-tested
> but still being brought up on hardware. **OTA signature verification is a dev
> integrity check, not a production signature**, and the mTLS identity flow
> needs a backend that isn't included. See
> [REQUIREMENTS_COVERAGE.md](REQUIREMENTS_COVERAGE.md) for the honest status.
>
> ⚡ **This drives mains-voltage relays.** Flashing alternative firmware and
> wiring mains is at your own risk. Know what you're doing.

## Why this exists

Most Tuya switch firmware is closed, cloud-dependent, and stops working when the
internet does. LibreSwitch is the opposite: the device is fully functional with
**zero connectivity**, the architecture is testable (192 host unit tests, no
hardware needed), and every hardware quirk we hit on the CB3S / BK7231N is documented
so you don't have to rediscover it. If you're converting a Tuya switch or
learning embedded clean-architecture, this is meant to be readable and
adaptable.

## Highlights

- **Local-first / device-authoritative** — touch always works; the network can
  only observe or *request*, never directly drive relays.
- **Hexagonal architecture** — pure domain logic behind ports; thin on-device
  adapters; 192 host tests run on every push (no hardware).
- **1–4 gang via profiles**, with all pins/polarity as **build flags** — adapt
  to a new board without editing C++.
- **Honest about its limits** — security placeholders and open items are
  labeled, not hidden.

## Quick start

```bash
# build + flash (LibreTiny + PlatformIO)
pio run -t upload

# watch the device (logs are on UART1)
pio device monitor -b 115200

# run the host test suite (no hardware needed)
./test/host/run_all.sh
```

First boot on a fresh device needs a one-time profile seed — see the bring-up
section below. Adapting to your specific switch is usually just a pin map; see
[CONTRIBUTING.md](CONTRIBUTING.md#adding-a-board--device-profile).

## Docs

- **[REQUIREMENTS_COVERAGE.md](REQUIREMENTS_COVERAGE.md)** — phase-by-phase
  status, what's proven on hardware vs. wired-but-untested, and the open items.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — how to engage, and how to add a board.
- **[SECURITY.md](SECURITY.md)** — security posture (read before deploying).

---

Open-source, local-first firmware for a BK7231N smart wall switch (1–4 gang via
device profiles). Built on LibreTiny + Arduino. Hexagonal architecture: a pure,
offline-authoritative **Core Domain** behind ports, with thin platform adapters
and a connectivity plane (MQTT / provisioning / OTA) that can never block local
control.

**Prime invariant:** local touch control works correctly regardless of network,
cloud, provisioning, or OTA state. Connectivity can only *observe* or *request* —
never act on relays directly. This is enforced structurally (the Control task
owns all relay writes; remote commands are queued, not executed inline) and
proven by tests.

## Status at a glance

- **190 host unit tests, 0 failures.** Every core-domain component ≥90% coverage.
- **All 10 architecture phases implemented AND wired** (see REQUIREMENTS_COVERAGE.md
  for the full phase-by-phase audit).
- **Hardware-proven on the BK7231N:** boot, FlashDB (kvs+userdata), profile
  resolve, touch→relay control, state persistence across power cycles,
  event logging, flat heap (~114 KB, zero drift).
- Wired but not yet hardware-proven: provisioning portal, MQTT/mTLS
  connect, OTA pipeline, SNTP. Three labeled placeholders remain
  (R-19 enrollment backend, R-23 production signature verifier, R-0 soak).
- **NOT yet shippable** — see the release gate section.

## Layout

```
src/
  domain/     pure control logic (relay, touch, coordinator, boot, indicator, crc)
  ports/      seams between domain and platform
  adapters/   GPIO relay/touch/indicator, FlashDB state persistence
  platform/   HAL seams + on-device impls (GPIO, FlashDB KVS, FreeRTOS, clock,
              critical-section, bounded/sync queues, FAL config)
  profiles/   1–4 gang device profiles (const, flash-resident) + resolver
  net/        MQTT (service/topics/payload/backoff), provisioning (portal +
              sequential creds test), OTA (verify-before-commit + health-confirm)
  log/        Phase 9 dual-clock event logger + TSDB sink
  app/        control loop, fan-out, command-sink, composition root
  main.cpp    entry: boot local plane, spawn Control task (+ Mqtt if provisioned)
test/host/    L1 host unit tests (no hardware) — run on every commit
```

## Build, test, flash

```bash
# host unit tests (no hardware needed)
./test/host/run_all.sh

# build firmware (requires PlatformIO + LibreTiny)
pio run -e bk7231n

# flash over serial
pio run -e bk7231n -t upload

# bench serial monitor — logs are on UART1 (R-0 finding):
# build sets LT_UART_DEFAULT_LOGGER=1 so logs land on the bench UART.
pio device monitor -b 115200
```

## What is verified, and what is not

**Verified on host (182 L1 tests):** all control logic; debounce + the
factory-reset gesture; boot policy incl. the R-7 safe fallback; profile
resolution + HW-compat; adapter logic (pin mapping, relay/touch polarity, PWM
gating); state persistence + CRC integrity; bounded + cross-task-synchronized
queues; the full assembled control path (touch→relay, command→relay); MQTT topic
building, payload codec, backoff, and command routing through the port (MQTT
never writes a relay directly); provisioning state machine incl. the sequential
creds-test ordering; OTA trust-layer sequencing (HW mismatch → no download;
verify-fail → abort, never commit) + the R-24 health-confirm; and the dual-clock
logging with NTP anchor + floor.

**NOT yet verified — on-device + backend (the honest boundary):**
the physical and cryptographic truths host tests cannot reach. See checklist.

## On-device bring-up checklist (first flash)

The LED + UART1 logs are your instruments. Do these in order.

1. **Boots & logs.** Flash, open UART1 @ 115200. Expect `[boot] ... up`.
   (R-33: tasks use dynamic `xTaskCreate` — confirm Control task starts.)
2. **FlashDB binds.** No FAL/KVDB/TSDB init error. Confirms the `kvdb`
   (0x1D8000) and `tsdb` (0x1E3000) partitions are reachable (Phase 4 map). If
   it fails, recheck `fal_cfg.h` offsets vs the platform `build.flash`.
3. **Profile loads.** A valid `profile_id` byte must exist in KVS (set at
   manufacture/provisioning). Missing/unknown → safe fault halt (by design).
   For first bring-up, pre-seed it (e.g. 4 for switch_4g).
4. **Relays click — and polarity is right.** Toggle each channel; confirm GPIO
   6/8/9/26 drive the intended relay and ON actually energizes. If inverted, set
   `RelayPolarity::ActiveLow` in the composition root.
5. **Touch reads — and active level is right.** Touch each pad (GPIO 24/20/7/14);
   confirm a press toggles the right channel. If inverted, set
   `TouchActiveLevel::ActiveLow`. (R-31: do NOT enable I2C1/SPI0 — they would
   steal GPIO20/GPIO14.)
6. **LED patterns.** Status LED (GPIO22) shows the expected pattern; backlight
   (GPIO23) is on/off only (R-32: not PWM-capable on this HW).
7. **Reset gesture.** Hold the two-button factory gesture (Q4); LED accelerates
   then goes solid at completion.
8. **State persists.** Toggle a relay, power-cycle; with RestoreLast policy the
   state returns (or boots OFF safely if persistence is absent/corrupt — R-7).
9. **Logging works.** Confirm events appear in the TSDB (boot, relay changes).
   Dual-clock: pre-NTP records carry uptime + floor; post-NTP carry real UTC.

## Connectivity bring-up (after local plane is solid)

10. **Provision.** Boot unprovisioned → the net task opens SoftAP
    `Switch-XXXX` (WPA2). The per-device SSID **and password are printed
    on UART** at portal-open. Join it, browse to http://192.168.4.1,
    submit Wi-Fi creds + enrollment token. The switch drops the AP to
    test creds sequentially (Beken can't do AP+STA); creds persist ONLY
    on success; failure restores the portal. On every later boot the
    stored creds connect directly. SNTP starts on first connect (the
    event log gains real UTC).
11. **mTLS connect (R-0, RELEASE-GATING).** MQTT enables itself only when
    `mqtt_host`, `device_id`, `tls_ca`, `tls_cert`, `tls_key` exist in
    KVS (until the R-19 backend exists, load them out-of-band). The
    literal mutual-TLS connect + 24 h soak is the open R-0 gate.
12. **OTA.** Publish a manifest to `<root>/<id>/cmd/system/ota`:

    ```
    url=https://host/fw.uf2;ver=0.2.0;sig=<base64 SHA-256 of image>;hw=bk7231n.relay_touch;ch=4;size=123456
    ```

    Pipeline: HW-compat precheck → anti-downgrade → MQTT yields TLS →
    streamed download to the staging slot → verify-before-commit →
    commit + reboot → **180 s health window** (local control + broker
    reconnect + an explicit `cmd/system/confirm` message) or automatic
    rollback. NOTE: the current verifier is the **dev integrity check**
    (SHA-256), not the production signature — see REQUIREMENTS_COVERAGE.md.

13. **Factory reset.** Hold the two-button gesture: creds, TLS material,
    relay state, and OTA flags are wiped; **profile_id is kept** (Q1);
    the device reboots into the provisioning portal.

## Release gate (Phase 10) — before any production build

- **R-0:** literal mutual-TLS connect + 24 h soak (release-gating; still open).
- **R-22:** override the LibreTiny default OTA AES key/iv (the `0123…`
  placeholders are public — fail-closed CI check).
- **R-23:** Ed25519 OTA signing key from HSM/CI signer, never the repo.
- **R-19:** enrollment-token issuance/validation is a backend contract.
- L5 adversarial tests pass.

## Build-flag reference

All hardware/config knobs are flags — no code edits needed:
`SEED_PROFILE_ID=<1..4>` one-time profile seed for a fresh device (then
remove); `RELAY_ACTIVE_LOW` flips relay polarity; `RELAY_PIN_0..3`,
`TOUCH_PIN_0..3`, `STATUS_LED_PIN`, `BACKLIGHT_PIN` retarget pins;
`FW_VER_MAJOR/MINOR/PATCH` set the running version for anti-downgrade.

## Known-open hardware behaviours

Inverted relay states after a power cycle and a brief all-ON flash at
boot were observed during bring-up and deprioritized. Both match an
active-LOW board signature (note: toggle-based control cannot reveal
polarity — every press flips the relay either way). A decisive
two-minute test and the boot-flash analysis (firmware now parks relay
pins in the first lines of setup(); the residual bootloader-phase flash
wants a hardware pull resistor) are documented in
REQUIREMENTS_COVERAGE.md.

## Honest boundaries

Host tests prove **logic**. They do not prove: physical GPIO/relay/touch/LED
behavior; FlashDB binding to real partitions; mTLS connecting to a real broker;
on-device Ed25519 verification and HTTPS image download; the enrollment CSR/cert
exchange; or BLE (out of scope — no LibreTiny BLE stack; SoftAP is the only
provisioning path). These are on-device + backend work, several gated on closing
R-0. The codebase is complete and internally consistent; turning it into a
shipped product is bring-up and integration, not more logic.
