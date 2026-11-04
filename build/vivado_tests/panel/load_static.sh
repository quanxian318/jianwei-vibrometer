#!/bin/sh
set -eu
STAGE=/root/panel_debug_20261031
DIR=/root/vibmon_cam
cd "$STAGE"
sha256sum -c static_files.sha256
mkdir -p before
sha256sum "$DIR/an5642_stereo_sgm40_pdm.bit.bin" "$DIR/vibmond" "$DIR/an5642_board.json" > before_runtime.sha256
cp "$DIR/an5642_board.json" before/an5642_board.json
if pgrep -x vibmond >/dev/null; then
 python3 - <<'PY'
import subprocess,json
d=json.loads(subprocess.check_output(['/root/vibmon_cam/vibmonctl','-s','/tmp/vibmon.sock','status']).decode())['data']
s=d['measurement']['state']
if s not in ('preview','idle','result','done'):
 raise RuntimeError('Measurement active; test not loaded: '+s)
PY
fi
finished=0
trap 'if [ "$finished" -eq 0 ]; then env -u SSH_ORIGINAL_COMMAND /bin/sh /root/vibmon_cam/start_ui_from_pc.sh start || true; fi' EXIT
pkill -TERM -x vibmond || true
i=0
while pgrep -x vibmond >/dev/null; do
 i=$((i+1)); test "$i" -lt 20 || exit 1; sleep 1
done
python3 "$DIR/prepare_an5642_runtime.py" "$STAGE/panel_static.bit.bin"
python3 "$STAGE/panel_static_diag.py" off
finished=1
echo STATIC_PANEL_TEST_READY
