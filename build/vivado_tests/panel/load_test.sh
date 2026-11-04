#!/bin/sh
set -eu
STAGE=/root/panel_debug_20261030
DIR=/root/vibmon_cam
cd "$STAGE"
sha256sum -c panel_files.sha256
sha256sum "$DIR/an5642_stereo_sgm40_pdm.bit.bin" "$DIR/vibmond" "$DIR/an5642_board.json" > before_runtime.sha256
cp "$DIR/an5642_board.json" before_board.json
systemctl stop vibmon-touch-guard.service
finished=0
trap 'if [ "$finished" -eq 0 ]; then env -u SSH_ORIGINAL_COMMAND /bin/sh /root/vibmon_cam/start_ui_from_pc.sh start || true; systemctl start vibmon-touch-guard.service || true; fi' EXIT
pkill -TERM -x vibmond || true
i=0
while pgrep -x vibmond >/dev/null; do
 i=$((i+1)); test "$i" -lt 20 || exit 1; sleep 1
done
python3 "$DIR/prepare_an5642_runtime.py" "$STAGE/panel.bit.bin"
python3 "$STAGE/panel_diag.py" status
systemctl start vibmon-touch-guard.service
finished=1
echo PANEL_TEST_LOADED_NORMAL_RUNTIME_FILES_UNCHANGED
