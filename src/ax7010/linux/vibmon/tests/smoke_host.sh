#!/bin/sh
# End-to-end smoke test on a Linux PC without carrier hardware: vibmond with
# the synthetic camera (5 Hz, 2 px), driven through vibmonctl.
#   make && sh tests/smoke_host.sh [build dir]
set -eu
B=${1:-build}
W=$(mktemp -d)
trap 'kill $PID 2>/dev/null || true; rm -rf "$W"' EXIT

cat > "$W/cal.json" <<EOF
{"id": "SMOKE-CAL", "mm_per_px": 0.05, "axis": "y"}
EOF
cat > "$W/vibmon.json" <<EOF
{
  "device_id": "smoke",
  "camera": {"source": "synthetic", "synthetic": {"freq_hz": 5, "amplitude_px": 2, "fps": 60}},
  "measurement": {"duration_s": 4, "calibration_file": "$W/cal.json", "roi_width_px": 48, "roi_height_px": 48, "search_radius_px": 6},
  "alarm": {"warn_peak_to_peak": 0.15, "alarm_peak_to_peak": 0.5},
  "storage": {"records_dir": "$W/records"},
  "ui": {"enabled": false},
  "control_socket": "$W/ctl.sock",
  "poweroff_on_shutdown": false
}
EOF
"$B/vibmond" -c "$W/vibmon.json" > "$W/log.txt" 2>&1 &
PID=$!
sleep 2
CTL="$B/vibmonctl -s $W/ctl.sock"
$CTL status > /dev/null
$CTL measure_start
sleep 6
$CTL status > "$W/status.json"
cat "$W/status.json"
grep -q '"valid": true' "$W/status.json"
grep -q '"level": "预警"' "$W/status.json"
ID=$(ls "$W/records")
test -f "$W/records/$ID/record.json"
test -f "$W/records/$ID/waveform.csv"
test -f "$W/records/$ID/spectrum.csv"
grep -q '"dominant_frequency_hz"' "$W/records/$ID/record.json"
if $CTL print > "$W/print.json"; then
  echo "print unexpectedly succeeded without a printer"
  exit 1
fi
grep -q no_printer_uart "$W/print.json"
$CTL shutdown
sleep 2
$CTL status | grep -q '"shutdown_prepared": true'
kill -TERM $PID
wait $PID || true
echo "smoke test passed ($ID)"
