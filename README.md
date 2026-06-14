# LibreSwitch - cloud-free firmware for Tuya smart switches
**Take your smart switch off the Tuya cloud.** LibreSwitch is open-source replacement firmware for BK7231N / CB3S wall switches - the chip inside many "Tuya" / Smart Life 1–4 gang touch switches. It's for people who want a smart switch without a vendor account, without the switch phoning home, and without anything in their house depending on someone else's servers.

- **No Tuya cloud, no account, no telemetry.** The firmware contains no Tuya code and never talks to Tuya. Nothing about your switches leaves your home unless you point it at a service you run.
- **Local touch always works - even fully offline.** Wi-Fi down, no internet, no broker configured? The buttons on the wall still drive the relays. It's enforced structurally (the Control task owns every relay write) and proven by tests.
- **Your network, your rules.** Remote control is opt-in and goes to your own MQTT broker (e.g. Mosquitto, or the broker Home Assistant uses) - never a vendor cloud. Configure nothing and it's a fully local device. (The MQTT path is implemented and host-tested but not yet proven on real hardware - see Status.)

Flash it yourself with [LibreTiny](https://docs.libretiny.eu/) - no Tuya developer account required. See [Flashing it (first install)](#flashing-it-first-install) below.

**Prime invariant:** local touch control works correctly regardless of network,
cloud, provisioning, or OTA state. Connectivity can only *observe* or *request* -
never act on relays directly. This is enforced structurally (the Control task
owns all relay writes; remote commands are queued, not executed inline) and
proven by tests.

## Privacy & FAQ
**What data leaves my home?** By default, none. The firmware contains no Tuya code and makes no outbound connections on its own. The only network traffic is to services you explicitly configure (your MQTT broker; optionally your own enrollment/OTA endpoint). Local touch control makes no network calls at all.

**Do I need a Tuya account or the Smart Life app?** No - never. Setup is done through the switch's own Wi-Fi portal (SoftAP). No vendor account, no app.

**Can I still use the switch if my internet - or even my LAN - is down?** Yes. The wall buttons drive the relays fully offline. This is the project's prime invariant: local control never depends on the network.

**Does it phone home / send telemetry / analytics?** No. There is no telemetry, analytics, or usage reporting of any kind, to anyone.

**Does it work with Home Assistant?** It speaks MQTT to a broker you run (e.g. Mosquitto, which Home Assistant uses) - never a vendor cloud. Honest status: native HA MQTT auto-discovery isn't implemented yet, and the MQTT path is host-tested but not yet proven on real hardware (see Status).

**It has TLS / enrollment / OTA code - is it really cloud-free?** Yes. All of that connects only to endpoints you configure and run; there is no default or baked-in server. Configure nothing and it stays a purely local switch. OTA pulls from a URL you control (and today uses a dev integrity check, not a production signature - see SECURITY.md).

*LibreSwitch began as a way to control these switches through a user-owned cloud (a personal Firebase project) instead of Tuya's. The shipping firmware keeps that "your backend, not the vendor's" principle but uses a standard MQTT path to a broker you run - including fully self-hosted, no-third-party setups.*

## Flashing it (first install)

These switches don't break TX/RX out to a header, so the first install is a one-time **serial (UART)** job with the case open. After that, LibreSwitch updates over the air on your own network - the open-case step is one-time.

> **⚠ Mains safety:** fully disconnect the switch from mains before opening it. Flash the module on the bench at **3.3 V only** - never with mains connected, and never apply 5 V (all BK7231N I/O is 3.3 V).

**You need**
- A **3.3 V** USB-to-UART adapter (CP2102 / CH340 at 3.3 V / PL2303 / FT232 set to 3.3 V).
- A solid **3.3 V supply** - the adapter's onboard regulator is often *not enough*; voltage sag mid-flash causes most failures. An external 3.3 V (bench PSU or an AMS1117 board) is recommended.
- [ltchiptool](https://github.com/libretiny-eu/ltchiptool), LibreTiny's flasher.
- A firmware image: build it (see [Build, test, flash](#build-test-flash)) and use the `.uf2` from `.pio/build/bk7231n/`.

**Wiring (CB3S / BK7231N)** - UART1, crossed:

| USB-UART adapter | CB3S pad |
| --- | --- |
| RX  | **TX1** (P11 / GPIO11) |
| TX  | **RX1** (P10 / GPIO10) |
| GND | **GND** |
| 3V3 | **3V3** (or external 3.3 V) |
| *(reset)* | **CEN** - momentarily bridge to GND to reboot into download mode |

<!-- Photo of the exact CB3S pads used. Add the image at docs/img/cb3s-flashing.jpg and uncomment:
![CB3S UART flashing pads](docs/img/cb3s-flashing.jpg) -->
_(Photo of the exact pads used: to be added.)_

**Steps**
1. Wire as above. Double-check GND and that you're on **3.3 V, not 5 V**.
2. In ltchiptool, select **BK7231N** - **not** BK7231T (choosing T can brick an N).
3. Pick the LibreSwitch `.uf2` and the COM port, then start writing.
4. While ltchiptool is "trying to connect," reboot the chip into download mode by briefly **bridging CEN to GND** (a wire, or a momentary button to GND, both work). Don't reset too fast or you'll get "No response."
5. Let it finish, disconnect the programmer, reassemble, and power up - first boot opens the Wi-Fi setup portal (`Switch-XXXX`).

**Other modules (WB3S, CBU, CB2S, CB3L, ...)**
LibreTiny supports many BK72xx modules and the method is identical (UART1 on TX1/RX1, CEN to reset) - only the physical pad positions differ. See the [LibreTiny BK72xx guide](https://docs.libretiny.eu/docs/platform/beken-72xx/) for the full module list and the [ltchiptool manual](https://docs.libretiny.eu/docs/flashing/tools/ltchiptool/). If a unit's stock firmware is exploitable, [tuya-cloudcutter](https://github.com/tuya-cloudcutter/tuya-cloudcutter) can flash it **without opening the case**.

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
- **NOT yet shippable** - see the release gate section.

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
test/host/    L1 host unit tests (no hardware) - run on every commit
```

## Build, test, flash

```bash
# host unit tests (no hardware needed)
./test/host/run_all.sh

# build firmware (requires PlatformIO + LibreTiny)
pio run -e bk7231n

# flash over serial
pio run -e bk7231n -t upload

# bench serial monitor - logs are on UART1 (R-0 finding):
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

**NOT yet verified - on-device + backend (the honest boundary):**
the physical and cryptographic truths host tests cannot reach. See checklist.

## On-device bring-up checklist (first flash)

The LED + UART1 logs are your instruments. Do these in order.

1. **Boots & logs.** Flash, open UART1 @ 115200. Expect `[boot] ... up`.
   (R-33: tasks use dynamic `xTaskCreate` - confirm Control task starts.)
2. **FlashDB binds.** No FAL/KVDB/TSDB init error. Confirms the `kvdb`
   (0x1D8000) and `tsdb` (0x1E3000) partitions are reachable (Phase 4 map). If
   it fails, recheck `fal_cfg.h` offsets vs the platform `build.flash`.
3. **Profile loads.** A valid `profile_id` byte must exist in KVS (set at
   manufacture/provisioning). Missing/unknown → safe fault halt (by design).
   For first bring-up, pre-seed it (e.g. 4 for switch_4g).
4. **Relays click - and polarity is right.** Toggle each channel; confirm GPIO
   6/8/9/26 drive the intended relay and ON actually energizes. If inverted, set
   `RelayPolarity::ActiveLow` in the composition root.
5. **Touch reads - and active level is right.** Touch each pad (GPIO 24/20/7/14);
   confirm a press toggles the right channel. If inverted, set
   `TouchActiveLevel::ActiveLow`. (R-31: do NOT enable I2C1/SPI0 - they would
   steal GPIO20/GPIO14.)
6. **LED patterns.** Status LED (GPIO22) shows the expected pattern; backlight
   (GPIO23) is on/off only (R-32: not PWM-capable on this HW).
7. **Reset gesture.** Hold the two-button factory gesture (Q4); LED accelerates
   then goes solid at completion.
8. **State persists.** Toggle a relay, power-cycle; with RestoreLast policy the
   state returns (or boots OFF safely if persistence is absent/corrupt - R-7).
9. **Logging works.** Confirm events appear in the TSDB (boot, relay changes).
   Dual-clock: pre-NTP records carry uptime + floor; post-NTP carry real UTC.

## Connectivity bring-up (after local plane is solid)

10. **Provision.** Boot unprovisioned → the net task opens SoftAP
    `Switch-XXXX` (WPA2). The per-device SSID **and password are printed
    on UART** at portal-open. Join it, browse to http://192.168.43.1
    (the Beken SoftAP default - confirm against the `AP IP =` boot log line),
    submit Wi-Fi creds + enrollment token. The switch drops the AP to
    test creds sequentially (Beken can't do AP+STA); creds persist ONLY
    on success; failure restores the portal. On every later boot the
    stored creds connect directly. SNTP starts on first connect (the
    event log gains real UTC). **Enrollment (R-19, Option B):** with
    `-D ENROLL_URL` set, after the STA join the device POSTs its token
    (form-entered, or factory-seeded via `SEED_ENROLL_TOKEN`) to the
    endpoint over server-auth TLS and stores the returned per-device mTLS
    identity (`tls_ca/cert/key` + `mqtt_host`); MQTT then connects on the
    next boot. Without `ENROLL_URL` the dev stand-in is used (no cert
    fetched - mTLS stays gated on out-of-band material).
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
    (SHA-256), not the production signature - see REQUIREMENTS_COVERAGE.md.

13. **Factory reset.** Hold the two-button gesture: creds, TLS material,
    relay state, and OTA flags are wiped; **profile_id is kept** (Q1);
    the device reboots into the provisioning portal.

## Release gate (Phase 10) - before any production build

- **R-0:** literal mutual-TLS connect + 24 h soak (release-gating; still open).
- **R-22:** override the LibreTiny default OTA AES key/iv (the `0123…`
  placeholders are public - fail-closed CI check).
- **R-23:** Ed25519 OTA signing key from HSM/CI signer, never the repo.
- **R-19:** enrollment-token issuance/validation is a backend contract.
- L5 adversarial tests pass.

## Build-flag reference

All hardware/config knobs are flags - no code edits needed:
`SEED_PROFILE_ID=<1..4>` one-time profile seed for a fresh device (then
remove); `RELAY_ACTIVE_LOW` flips relay polarity; `RELAY_PIN_0..3`,
`TOUCH_PIN_0..3`, `STATUS_LED_PIN`, `BACKLIGHT_PIN` retarget pins;
`FW_VER_MAJOR/MINOR/PATCH` set the running version for anti-downgrade.

Enrollment (R-19, Option B): `ENROLL_URL` points the device at the
token→identity endpoint (enables real enrollment; absent → dev stand-in);
`ENROLL_CA_PEM` pins that endpoint's CA (required unless `ENROLL_INSECURE`
is set for dev); `SEED_ENROLL_TOKEN='"<tok>"'` one-time factory-seeds the
token so the installer enters only Wi-Fi (then remove, like the profile seed).

## Known-open hardware behaviours

Inverted relay states after a power cycle and a brief all-ON flash at
boot were observed during bring-up and deprioritized. Both match an
active-LOW board signature (note: toggle-based control cannot reveal
polarity - every press flips the relay either way). A decisive
two-minute test and the boot-flash analysis (firmware now parks relay
pins in the first lines of setup(); the residual bootloader-phase flash
wants a hardware pull resistor) are documented in
REQUIREMENTS_COVERAGE.md.

## Honest boundaries

Host tests prove **logic**. They do not prove: physical GPIO/relay/touch/LED
behavior; FlashDB binding to real partitions; mTLS connecting to a real broker;
on-device Ed25519 verification and HTTPS image download; the enrollment CSR/cert
exchange; or BLE (out of scope - no LibreTiny BLE stack; SoftAP is the only
provisioning path). These are on-device + backend work, several gated on closing
R-0. The codebase is complete and internally consistent; turning it into a
shipped product is bring-up and integration, not more logic.
