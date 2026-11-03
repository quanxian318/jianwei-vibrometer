#!/bin/sh
# pdm_mic against golden vectors from linux/vibmon/tools/pdm_vectors.
# Usage: run_tests.sh [vectors.txt ...]   (default: the committed vectors)
# Vectors are copied to an ASCII temp path: Icarus $fopen rejects non-ASCII names.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
iverilog -g2005 -Wall -s tb_pdm_mic -o "$tmp/tb" "$here"/../rtl/*.v "$here/tb_pdm_mic.v"
[ $# -gt 0 ] || set -- "$here"/vectors/*.txt
for v in "$@"; do
  echo "== $(basename "$v")"
  cp "$v" "$tmp/vec.txt"
  vvp -n "$tmp/tb" +VEC="$tmp/vec.txt"
done
