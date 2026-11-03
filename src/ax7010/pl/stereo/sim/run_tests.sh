#!/bin/sh
# PL stereo core against golden vectors from linux/vibmon/tools/sgm_vectors.
#   tb_sgm_core:    census in -> disparity (sgm_core alone)
#   tb_stereo_core: gray in   -> disparity (census_cs97 + sgm_core)
#   tb_stereo_axi:  block-design wrapper (AXI4-Lite registers, AXI4-Stream with stalls, IRQ)
# Usage: run_tests.sh [vectors.txt ...]  (default: ../vectors/*.txt)
# Vectors are copied to an ASCII temp path: Icarus $fopen rejects non-ASCII names.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
iverilog -g2005 -Wall -s tb_sgm_core -o "$tmp/tb_sgm" "$here/../rtl/sgm_core.v" "$here/tb_sgm_core.v"
iverilog -g2005 -Wall -s tb_stereo_core -o "$tmp/tb_top" "$here"/../rtl/*.v "$here/tb_stereo_core.v"
iverilog -g2005 -Wall -s tb_stereo_axi -o "$tmp/tb_axi" "$here"/../rtl/*.v "$here/tb_stereo_axi.v"
[ $# -gt 0 ] || set -- "$here"/../vectors/*.txt
for v in "$@"; do
  echo "== $(basename "$v")"
  cp "$v" "$tmp/vec.txt"
  vvp -n "$tmp/tb_sgm" +VEC="$tmp/vec.txt"
  vvp -n "$tmp/tb_top" +VEC="$tmp/vec.txt"
  vvp -n "$tmp/tb_axi" +VEC="$tmp/vec.txt"
done
