#!/bin/sh
# Packs the sources the vibmon recipe needs (this app + shared/vibproto, same
# relative layout) into vibmon-src.tar.gz.
#   sh tools/make_src_tarball.sh <output dir>
set -eu
OUT=$(cd "${1:-.}" && pwd)
HERE=$(cd "$(dirname "$0")/.." && pwd)          # .../src/ax7010/linux/vibmon
ROOT=$(cd "$HERE/../../.." && pwd)               # .../06_软件与FPGA
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/vibmon-src/shared" "$TMP/vibmon-src/ax7010/linux"
cp -r "$ROOT/shared/vibproto" "$TMP/vibmon-src/shared/"
mkdir -p "$TMP/vibmon-src/ax7010/linux/vibmon"
(cd "$HERE" && tar cf - --exclude=build Makefile src tools tests config $( [ -d third_party ] && echo third_party )) |
  (cd "$TMP/vibmon-src/ax7010/linux/vibmon" && tar xf -)
tar czf "$OUT/vibmon-src.tar.gz" -C "$TMP" vibmon-src
echo "wrote $OUT/vibmon-src.tar.gz"
