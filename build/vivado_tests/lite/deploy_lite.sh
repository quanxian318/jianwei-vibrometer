#!/bin/sh
set -eu
STAGE=/root/lite_debug_20261031
DIR=/root/vibmon_cam
cd "$STAGE"
sha256sum -c files.sha256
test ! -e before/DEPLOYED
mkdir -p before
cp -p "$DIR/an5642_stereo_sgm40_pdm.bit.bin" before/normal.bit.bin
cp -p "$DIR/an5642_board.json" before/board.json
cp -p "$DIR/start_an5642.sh" before/start_an5642.sh
sha256sum "$DIR/vibmond" > before/app.sha256
pkill -TERM -x vibmond || true
i=0
while pgrep -x vibmond >/dev/null; do i=$((i+1)); test "$i" -lt 20; sleep 1; done
cp lite.bit.bin "$DIR/an5642_stereo_lite.bit.bin"
cp lite_board.json "$DIR/an5642_board.json"
python3 - <<'PY'
p='/root/vibmon_cam/start_an5642.sh'
with open(p) as f:s=f.read()
old='BIT=$DIR/an5642_stereo_sgm40_pdm.bit.bin'
assert old in s
with open(p,'w') as f:f.write(s.replace(old,'BIT=$DIR/an5642_stereo_lite.bit.bin'))
PY
env -u SSH_ORIGINAL_COMMAND /bin/sh "$DIR/start_ui_from_pc.sh" start
touch before/DEPLOYED
echo LITE_UI_LAUNCH_REQUESTED
