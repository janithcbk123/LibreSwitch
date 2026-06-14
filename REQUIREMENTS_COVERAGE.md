# Requirements Coverage — full audit (all 10 phases vs. the code)

Audited against the implementation as of this package. "Hardware-proven"
means confirmed on the physical BK7231N during bring-up; "host-proven"
means covered by the 190-test host suite; "wired, untested on HW" means
the code path exists and its pieces are host-tested, but the integrated
behaviour has not yet run on the device.

| Phase | Requirement | Status | Evidence / Notes |
|---|---|---|---|
| 1 | Local-first, device-authoritative control; touch always works regardless of connectivity | **Hardware-proven** | Touch toggles relays with MQTT disabled; control task owns all relay writes; remote commands queue, never execute inline |
| 1 | <50 ms local control budget | Hardware-proven (qualitative) | 10 ms control cadence; crisp response observed on device |
| 2 | 1–4 gang via const flash-resident profiles; profile survives factory reset (Q1) | **Hardware-proven** | `profile resolved, id=4` on device; factory reset wipe list explicitly KEEPS `profile_id`; pins now build-flag overridable (`-D RELAY_PIN_0..3`, `TOUCH_PIN_0..3`, `STATUS_LED_PIN`, `BACKLIGHT_PIN`) |
| 3 | Task split: Control p5 / Net p2; cross-task queues; no heap in steady state | **Hardware-proven** | Both tasks run; heap flat at ~114 KB across samples (zero drift); SyncQueue + `vPortEnterCritical` (Beken macro quirk found on HW) |
| 4 | Flash map + persistent storage | **Hardware-proven, adapted** | Platform auto-generates FAL partitions from the board JSON — we bind `kvs` (0x1D8000) and `userdata` (0x1E3000). KVS + relay-state persistence confirmed on HW (`persisted=FOUND`) |
| 4 | TSDB event log in userdata | **Adapted (KVDB ring)** | Platform FlashDB 1.2.0 is compiled KVDB-only (no TSDB). Log sink reimplemented as a KVDB ring behind the same seam; Logger + tests unchanged. Binds on HW |
| 5 | mTLS, per-device identity, on-device keygen→CSR→cert | **Adapted (Option B)** | mTLS client path wired (PubSubClient over MbedTLSClient; TLS material from KVS; `mbedtls_net_set_nonblock` Beken gap shimmed). **Platform finding (verified in `tls_config.h`):** the active Beken mbedTLS has `PK_WRITE_C`/`X509_CSR_WRITE_C`/`ECDSA_C`/`GENPRIME` compiled OUT — on-device keygen→CSR is NOT possible without recompiling mbedTLS. Chosen path = R-19 **Option B**: the backend mints the keypair+cert, the device fetches them over server-auth TLS (`HttpEnrollment`). Backend endpoint still pending |
| 6 | MQTT: capability topics, LWT, clean session, backoff+jitter, 512 B buffer | Host-proven; **wired, untested on HW** | Full service host-tested incl. connect params, routing, backoff. Needs a provisioned broker to prove on HW (R-0 literal connect still the release gate) |
| 6 | MQTT can never act on relays directly | Host-proven | Inbound commands → `ControlCommandPort` → queue → Control task. New system-topic routing tested to NOT leak into the relay path |
| 7 | SoftAP manual portal (no DNSServer), sequential cred test, WPA2, persist-only-on-success | Host-proven; **now wired** (was missing) | **Audit finding:** the portal was never started — an unprovisioned device could never be provisioned. Now: net task always runs provisioning; portal SSID/pass derived per-device from MAC and printed on UART; stored creds → direct STA connect at every boot |
| 7 | One-time enrollment token (R-19) | **Device-side done; backend pending** | Option B implemented: `HttpEnrollment` POSTs the token (portal-entered or factory-seeded via `SEED_ENROLL_TOKEN`) to `ENROLL_URL` over server-auth TLS and persists the returned per-device mTLS identity (`tls_ca/cert/key` + `mqtt_host`); the response wire-format codec is host-tested (`enrollment_response.h`). Server is PINNED via `ENROLL_CA_PEM` — the device refuses to send the token to an unauthenticated endpoint (unless `ENROLL_INSECURE`, dev only). `KvsDevEnrollment` stays as the no-backend fallback. REMAINING = the endpoint itself (one-time-token issuance/validation + key/cert minting) — the backend half of the contract |
| 8 | OTA: HW-compat precheck, verify-before-commit, anti-downgrade + signed escape hatch, 664 KB slot | Host-proven; **now wired** (was missing) | **Audit finding:** no inbound path triggered OTA and no downloader/verifier impls existed. Now: `cmd/system/ota` → manifest codec (new, host-tested) → OtaService pipeline; HTTP(S) streaming downloader (1 KB chunks, never buffers the image); MQTT yields TLS first (exclusive-TLS) |
| 8 | Layer-2 signature = Ed25519 with pinned key | **OPEN — dev integrity check only** | Platform mbedTLS (BDK 2.x) has NO Ed25519. `DevSha256Verifier` checks base64(SHA-256) from the manifest = integrity, NOT authenticity. Loudly labeled DO-NOT-SHIP. Options: vendor compact ed25519, or move signing to ECDSA-P256 (supported). Release-gated with R-23 |
| 8 | R-24 health confirm: 4 criteria within 180 s else rollback | **Now wired** (was missing) | `ota_pending` flag armed by OtaService pre-reboot; next boot: scheduler+local marks from control tick, network from MQTT connect, explicit confirm via `cmd/system/confirm`; timeout → `rollBack()` + reboot |
| 9 | Dual-clock logging (uptime always; UTC when NTP) + anchor + KVS floor | Host-proven; **SNTP now wired** (was missing) | **Audit finding:** nothing ever started SNTP, so UTC never became valid. Now `startSntpOnce()` on Wi-Fi Operational (verified: platform lwipopts wires SNTP→settimeofday). Relay events log on HW |
| 9 | Factory-reset gesture (Q4) → action | **Now wired** (was missing) | **Audit finding:** the gesture set a flag nobody consumed. Now: control tick consumes it → wipes creds/state (keeps `profile_id`) → reboot into portal |
| 10 | Release gates | **OPEN (unchanged)** | R-0 literal mTLS connect + 24 h soak; R-19 enrollment backend; R-22 override platform OTA AES key; R-23 real signature verifier + HSM signing; L5 adversarial tests |

## Known-open hardware behaviours (deprioritized by owner; documented)

**Inverted relay states after power cycle + brief all-ON flash at boot.**
Both are textbook signatures of active-LOW relay wiring, and a key earlier
observation deserves correction: *toggle-based control cannot reveal
polarity* — every press flips the relay under either polarity, so "touch
works correctly" never established ActiveHigh. The earlier ActiveLow test
was likely confounded: the snapshot persisted before the reflash was saved
under the old (inverted) mapping, so the first reboot after switching
polarity reproduces the inversion once. Two-minute decisive test, when
desired: build with `-D RELAY_ACTIVE_LOW`, toggle relays to a known
pattern, power-cycle **twice** (toggling to a fresh pattern between
cycles), and compare the second restore against `relays after boot:` on
UART. Firmware now parks relay pins to the safe level in the first lines
of `setup()` (window shrunk from ~400 ms to near-zero of firmware time);
the residual bootloader-phase flash is hardware territory — a pull
resistor per relay control line holding the de-energized level is the
proper fix.

## What "make it work" still requires outside this codebase

The firmware is now feature-complete against the architecture with three
labeled placeholders: the enrollment **backend endpoint** (R-19 — the
device-side token→identity client is now implemented, Option B; only the
server that mints the cert is outstanding), the production signature
verifier (R-23/Ed25519-vs-ECDSA decision), and the R-0 soak.
Everything else — local control, persistence, provisioning portal, MQTT,
OTA pipeline with health-confirm, logging with real UTC, factory reset —
is implemented, host-tested (190/0), and wired end-to-end.
