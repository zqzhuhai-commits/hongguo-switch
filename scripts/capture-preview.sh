#!/bin/sh
set -eu
mkdir -p build/screenshots 'sdmc:'
ln -sfn romfs 'romfs:'
ffmpeg -v error -f lavfi -i 'testsrc2=size=960x540:rate=25' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 30 -c:v libx264 -pix_fmt yuv420p -c:a aac -y build/preview-video.mp4
for screen in home category search detail player player-menu favorites history downloads settings settings-decoder settings-player; do
  HONGGUO_PREVIEW_SCREEN="$screen" build-preview/hongguo-preview >"build/preview-$screen.log" 2>&1 &
  preview_pid=$!
  sleep 5
  if ! kill -0 "$preview_pid" 2>/dev/null; then cat "build/preview-$screen.log"; exit 1; fi
  import -window root "build/screenshots/$screen.png"
  kill "$preview_pid"
  wait "$preview_pid" || true
done
python3 - <<'PY'
from pathlib import Path
Path('build/screenshots/README.txt').write_text('这些截图由与 Switch 版本共用的原生 C++ 界面在桌面 OpenGL 环境运行产生。使用演示目录、渐变封面和测试视频；用于检查布局与控件，不是 Switch 实机截图，也不证明所有线上剧集可播。实际音视频播放验证见 playback-probe.json。\n')
PY
