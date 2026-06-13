#!/usr/bin/env bash
# One-time helper to initialize the LibreSwitch git repo and first commit.
# Run from the project root. Review before running.
set -e

git init
git add .
git commit -m "Initial public release: LibreSwitch BK7231N smart-switch firmware

Local-first, device-authoritative firmware on LibreTiny. Local plane proven
on hardware (boot, FlashDB, touch->relay, state persistence). Provisioning/
MQTT/OTA wired and host-tested (192/0). See REQUIREMENTS_COVERAGE.md for the
honest status and open items."

echo
echo "Local repo created. Next:"
echo "  1. Create an empty repo on GitHub (no README/license — we have those)."
echo "  2. git remote add origin https://github.com/<you>/<repo>.git"
echo "  3. git branch -M main"
echo "  4. git push -u origin main"
echo
echo "Then on GitHub: enable Discussions (Settings > Features) and update the"
echo "USER/REPO placeholders in README badges + .github/ISSUE_TEMPLATE/config.yml."
