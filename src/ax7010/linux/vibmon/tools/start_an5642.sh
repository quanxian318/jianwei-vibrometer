#!/bin/sh
# Starts vibmond on the bare AX7010 with the AN5642 cameras (ALINX Debian
# image prepared by prepare_an5642_image.py): PL bitstream and clocks, then
# the touchscreen, then the program in the foreground
# (systemd: vibmon-an5642.service).
#   start_an5642.sh [dir]   dir holds vibmond, an5642_board.json,
#                           prepare_an5642_runtime.py and the .bit.bin
set -e
DIR=${1:-/root/vibmon_cam}
BIT=$DIR/an5642_stereo_sgm40_pdm.bit.bin

python3 "$DIR/prepare_an5642_runtime.py" "$BIT"

# The D-WAV touch controller of the Waveshare 7" HDMI LCD (H) gets an input
# device only from hid-generic, as with hid.ignore_special_drivers=1 on the
# kernel command line. Loading the PL upsets the panel's USB device (-71
# errors): it is reset and enumerated again afterwards, then bound to
# hid-generic. vibmond also picks it up later if it comes after the start.
echo 1 > /sys/module/hid/parameters/ignore_special_drivers
for dev in /sys/bus/usb/devices/*; do
  [ "$(cat "$dev/idVendor" 2>/dev/null)" = 0eef ] || continue
  node=$(printf '/dev/bus/usb/%03d/%03d' "$(cat "$dev/busnum")" "$(cat "$dev/devnum")")
  # USBDEVFS_RESET
  python3 -c 'import fcntl, sys; fcntl.ioctl(open(sys.argv[1], "wb"), 0x5514)' "$node" || true
  echo 0 > "$dev/authorized" || true
  sleep 1
  echo 1 > "$dev/authorized" || true
done
i=0
while [ $i -lt 20 ] && ! grep -qs 0eef /sys/class/input/event*/device/id/vendor; do
  sleep 0.5
  i=$((i + 1))
done

exec "$DIR/vibmond" -c "$DIR/an5642_board.json"
