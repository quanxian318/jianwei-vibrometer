#!/bin/sh
set -eu
STAGE=/root/lite_debug_20261031
DIR=/root/vibmon_cam
cd "$STAGE"
pkill -TERM -x vibmond || true
i=0
while pgrep -x vibmond >/dev/null; do i=$((i+1)); test "$i" -lt 20; sleep 1; done
sha256sum -c before/app.sha256
cp -p before/board.json "$DIR/an5642_board.json"
cp -p before/start_an5642.sh "$DIR/start_an5642.sh"
env -u SSH_ORIGINAL_COMMAND /bin/sh "$DIR/start_ui_from_pc.sh" start
