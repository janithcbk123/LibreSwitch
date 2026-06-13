# Contributing to LibreSwitch

Thanks for your interest! First, an honest framing of what this project is:

**LibreSwitch is shared primarily as a showcase and a starting point.** It's a
working, open firmware for BK7231N smart switches that you're free to use,
fork, and adapt under the MIT license. Issues, ideas, and pull requests are
genuinely welcome — but please set expectations accordingly: this is
maintained in spare time, reviews may be slow, and not every PR will be
merged. Forking and adapting for your own board is absolutely encouraged and
needs no permission.

## Good ways to contribute

- **Report what you find.** Hardware quirks on a specific Tuya module, a
  LibreTiny version mismatch, a build error — open an issue. Even just
  "this works on board X with these pins" is valuable to the next person.
- **Share board profiles.** If you map the GPIOs for a new switch variant,
  that's the single most useful contribution — see "Adding a board" below.
- **Small, focused PRs.** A bug fix, a doc clarification, a new device
  profile. Big architectural changes are better discussed in an issue first.

## Before you open a PR

1. **Keep the host tests green.** Run `./test/host/run_all.sh` — it must pass
   (no hardware needed). New logic should come with a test.
2. **Verify platform APIs against the installed LibreTiny source**, not from
   memory. This codebase has been bitten repeatedly by training-data / docs
   assumptions that didn't match the platform's actual bundled libraries
   (FlashDB version, HTTPClient, time functions). When in doubt, grep the
   platform package.
3. **Prefer build-flag config over code edits** for anything hardware-specific
   (pins, polarity). See `platformio.ini` and `src/profiles/`.
4. **Don't commit secrets.** No certs, keys, or Wi-Fi credentials. `.gitignore`
   covers the common cases, but double-check.

## Adding a board / device profile

Most "make it work on my switch" needs are just a pin map. You usually don't
need to touch C++ at all — override pins with build flags:

```ini
build_flags =
    -D RELAY_PIN_0=...  -D RELAY_PIN_1=...  -D RELAY_PIN_2=...  -D RELAY_PIN_3=...
    -D TOUCH_PIN_0=...  -D TOUCH_PIN_1=...  -D TOUCH_PIN_2=...  -D TOUCH_PIN_3=...
    -D STATUS_LED_PIN=...  -D BACKLIGHT_PIN=...
    -D RELAY_ACTIVE_LOW       ; if your relays energize on LOW
    -D TOUCH_ACTIVE_LOW       ; if your touch pads idle HIGH
```

If you've confirmed a mapping on real hardware, please share it in an issue or
a small PR adding a documented profile — it directly helps others with the
same module.

## Safety note

This firmware drives **mains-voltage relays**. Contributions that touch relay
control, boot state, or the factory-reset path get extra scrutiny, because a
bug there can leave a device in an unsafe or unrecoverable state. The
`control_coordinator` prime invariant (local touch always works, independent
of connectivity) and the `boot_policy` safe fallback are load-bearing — please
preserve them.

## Not yet production-ready

See `REQUIREMENTS_COVERAGE.md` for the honest status. Notably the OTA signature
verification is a **development integrity check, not a production signature**,
and the enrollment/mTLS identity flow needs a backend that isn't included.
Please don't ship this to real installations without addressing the items
listed there.
