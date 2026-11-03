#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/.build/host"
cd "$root/src/ax7010/linux/vibmon"
make -j4 BUILD="$out" "$out/test_drivers" "$out/test_vibration_band" \
  "$out/test_multipoint" "$out/test_render" "$out/test_uart16550" "$out/vibmond"
for test in test_drivers test_vibration_band test_multipoint test_render test_uart16550; do
  "$out/$test"
done
python3 tests/test_alarm_consistency_host.py "$out/vibmond"
