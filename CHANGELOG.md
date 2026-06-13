# Changelog

Notable changes and the hard-won platform lessons behind them. The "why" here
is deliberately preserved — these are exactly the BK7231N/LibreTiny gotchas
that are hard to find documented elsewhere.

## [Unreleased] — initial public release prep

### Hardware bring-up (proven on a real CB3S / BK7231N 4-gang switch)
- Boot, FlashDB binding, profile resolution, touch→relay control, and relay
  **state persistence across power cycles** all confirmed on-device.
- Flat heap (~114 KB idle, zero drift) — the static-allocation, no-heap-in-
  steady-state discipline holds up on real silicon.
- Per-state-change UART logging (`[state] ch=N ON/OFF src=...`) for bench
  observability.

### Platform-integration lessons (the expensive ones)
- **FlashDB is platform-bundled, not a registry dep.** LibreTiny ships FlashDB
  1.2.0 (KVDB-only, no TSDB) and *auto-generates* the FAL partition table from
  the board flash-map JSON. Do not vendor FlashDB or declare partitions —
  vendoring a different version causes a wall of struct-mismatch errors. Use
  the platform partitions `kvs` and `userdata`. The event log was rewritten as
  a KVDB ring (behind a seam) because there is no TSDB.
- **FreeRTOS critical sections:** this Beken port's `taskENTER/EXIT_CRITICAL`
  macros are not statement-safe in one-line inline methods. Use
  `vPortEnterCritical()` / `vPortExitCritical()`.
- **mbedTLS:** the Beken port is missing `mbedtls_net_set_nonblock` (LibreTiny
  only ships that fixup for Realtek). Provided in
  `src/platform/mbedtls_net_fixup.c` via `lwip_fcntl`.
- **Time:** call `gettimeofday()` (routed through the platform's
  `__wrap_gettimeofday`), not newlib `time()` — the latter drags `libnosys`'s
  `_gettimeofday` into the link and collides with the platform's definition.
- **OTA download avoids `HTTPClient`** (it pulls in `strptime`/`_gettimeofday`
  and fails to link); the downloader does a hand-rolled HTTP GET over
  `WiFiClient` / `WiFiClientSecure` instead.

### Reliability fixes
- **Factory-reset boot loop fixed.** A touch read asserted at boot (e.g. wrong
  touch active-level) satisfied the two-button reset gesture and looped the
  device. The gesture now requires a clean released baseline after a settle
  window before it can arm — a stuck/boot-time press can never trigger a reset.
- **Relay safe-park at boot** (`parkRelaysSafe()` runs first in `setup()`) to
  shrink the boot-time relay flash window.

### Configurability
- All pins and polarities are build-flag overridable: `RELAY_PIN_0..3`,
  `TOUCH_PIN_0..3`, `STATUS_LED_PIN`, `BACKLIGHT_PIN`, `RELAY_ACTIVE_LOW`,
  `TOUCH_ACTIVE_LOW`, plus `SEED_PROFILE_ID` and `FW_VER_*`.

### Known open (see REQUIREMENTS_COVERAGE.md)
- Provisioning portal hardware bring-up (HTTP server reachable but timeout from
  client under investigation; low free RAM with SoftAP+WebServer is a suspect).
- MQTT/mTLS connect, OTA download/verify, SNTP — wired + host-tested, not yet
  exercised on hardware.
- OTA signature = dev integrity check, not production signature (no Ed25519 in
  platform mbedTLS); enrollment backend (mTLS identity) not included.
