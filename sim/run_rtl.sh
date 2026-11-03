#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$root/src/ax7010/pl/audio/sim/run_tests.sh"
sh "$root/src/ax7010/pl/displacement/sim/run_tests.sh"
sh "$root/src/ax7010/pl/stereo/sim/run_tests.sh"
