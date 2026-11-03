#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root/src/ax7010/linux/vibmon"
make -j4 BUILD="$root/.build/arm" CXX=arm-linux-gnueabihf-g++ \
  CC=arm-linux-gnueabihf-gcc AR=arm-linux-gnueabihf-ar \
  CXXFLAGS='-O2 -g -mfpu=neon -mfloat-abi=hard' \
  CFLAGS='-O2 -g -mfpu=neon -mfloat-abi=hard' \
  LDFLAGS='-static -pthread' "$root/.build/arm/vibmond"
