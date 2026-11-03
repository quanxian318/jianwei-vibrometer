#!/bin/sh
# Run from any directory. A failing assertion, unknown signal or simulation timeout returns nonzero.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -f "$tmp/tb"; rmdir "$tmp"' EXIT HUP INT TERM
iverilog -g2005 -Wall -s tb_carrier_ctrl -o "$tmp/tb" "$here"/../rtl/*.v "$here/IOBUF.v" "$here/tb_carrier_ctrl.v"
vvp "$tmp/tb"
