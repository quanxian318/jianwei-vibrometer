#!/bin/sh
# Reproduce 13_测振算法推进_20261021 from the two user clips (Linux/WSL, needs ffmpeg/ffprobe).
# usage: run_clips.sh <vibmon build dir with clip_track> [work dir for decoded frames]
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root="$here/.."
clips="$root/../12_视频验证_20261021/原始视频"
build=$1
work=${2:-$(mktemp -d)}
run() {  # stem outdir rois...
  stem=$1; out=$2; shift 2
  ffmpeg -v error -i "$clips/$stem.mp4" -map 0:v:0 -vsync 0 -pix_fmt gray -f rawvideo "$work/$stem.gray"
  ffprobe -v error -select_streams v:0 -show_entries frame=best_effort_timestamp_time -of csv=p=0 \
    "$clips/$stem.mp4" > "$work/$stem.pts"
  for k in lk ncc; do
    "$build/clip_track" "$work/$stem.gray" 1280 720 "$work/$stem.pts" "$out" $k 8 "$@" 2> "$out/log_$k.txt"
  done
  rm -f "$work/$stem.gray"
}
# ROIs (32x32, top-left corner) chosen from the first-frame texture map; not tuned on displacement spectra.
run a3332b91761880775aa172de0af3bad7 "$root/video1" \
  tip_upper:852:190:32 tip_lower:852:512:32 edge_lower_mid:544:514:32 edge_upper_mid:544:192:32 motor_body:115:280:32:ref
run cfb2f104a3598cc4c02675bb049f8b5b "$root/video2" \
  tip_upper:830:165:32 tip_lower:890:318:32 plate_mid:624:251:32 plate_root:371:424:32 wall_ref:1117:344:32:ref
