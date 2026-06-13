#!/usr/bin/env bash
# L1 host unit-test runner (Phase 10). Builds + runs every test_*.cpp,
# reports pass/fail and core-domain coverage. Zero external deps.
set -u
cd "$(dirname "$0")"
FAIL=0
for f in test_*.cpp; do
  bin="/tmp/$(basename "$f" .cpp)"
  if ! g++ -std=c++17 -Wall -Wextra -O0 "$f" -o "$bin" 2>/tmp/build.log; then
    echo "BUILD FAIL: $f"; cat /tmp/build.log; FAIL=1; continue
  fi
  "$bin" || FAIL=1
  echo
done
exit $FAIL
