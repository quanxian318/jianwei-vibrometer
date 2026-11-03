#!/bin/sh
# lk_accum against golden vectors. Usage: run_tests.sh [vectors.txt ...]
# Default: the committed vectors next to this script (synthetic texture and a user clip).
# Vectors are copied to an ASCII temp path: Icarus $fopen rejects non-ASCII names.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
iverilog -g2005 -Wall -s tb_lk_accum -o "$tmp/tb" "$here/../rtl/lk_accum.v" "$here/tb_lk_accum.v"
[ $# -gt 0 ] || set -- "$here"/vectors/*.txt
for v in "$@"; do
  echo "== $(basename "$v")"
  cp "$v" "$tmp/vec.txt"
  vvp -n "$tmp/tb" +VEC="$tmp/vec.txt"
done
