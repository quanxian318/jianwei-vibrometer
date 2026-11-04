#!/bin/sh
set -eu
STAGE=/root/panel_debug_20261031
python3 "$STAGE/panel_static_diag.py" off || true
cd "$STAGE"
sha256sum -c before_runtime.sha256
env -u SSH_ORIGINAL_COMMAND /bin/sh /root/vibmon_cam/start_ui_from_pc.sh start
