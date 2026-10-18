#!/bin/sh
set -eu
mkdir -p build/screenshots 'sdmc:'
ln -sfn romfs 'romfs:'
for screen in home category detail player-menu; do
  HONGGUO_LIVE=1 HONGGUO_PREVIEW_SCREEN="$screen" build-preview/hongguo-preview >"build/live-$screen.log" 2>&1 &
  preview_pid=$!
  ready=false
  for attempt in $(seq 1 85); do
    sleep 2
    if ! kill -0 "$preview_pid" 2>/dev/null; then cat "build/live-$screen.log"; exit 1; fi
    trace='sdmc:/switch/hongguo-saf/runtime.log'
    if [ "$screen" = player-menu ]; then
      if grep -q 'playback:loaded:' "$trace"; then ready=true; break; fi
    elif [ "$screen" = detail ]; then
      if grep -q 'api:detail:complete' "$trace" && grep -q 'poster:upload_end' "$trace"; then ready=true; break; fi
    elif [ "$(grep -c 'poster:upload_end' "$trace" || true)" -ge 4 ]; then ready=true; break
    fi
  done
  if [ "$ready" != true ]; then cat "$trace"; kill "$preview_pid"; exit 1; fi
  sleep 6
  import -window root "build/screenshots/live-$screen.png"
  cp "$trace" "build/live-$screen.trace"
  kill "$preview_pid"
  wait "$preview_pid" || true
done
python3 - <<'PY'
from pathlib import Path
Path('build/screenshots/LIVE-README.txt').write_text('live- 前缀为同一原生界面连接线上目录、HEIC 封面和实际取流后的桌面截图。播放器为《首辅娇娘》第 31 集。其余截图为演示数据布局预览。全部均非 Switch 实机截图。音视频解码结果见 playback-matrix.json，真实封面解码结果见 cover-probe.json。\n')
PY
