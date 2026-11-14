#!/usr/bin/env bash
# The diskOS UI dev loop, run inside the dev container (.devcontainer/, docs/DEV_WORKFLOW.md). Builds are plain
# CMake presets (`cmake --build --preset device` in ui/, or CMake Tools); this script does the rest.
#
#   tools/diskos-dev.sh setup        Python environment, git hooks and CMake configure (the container runs it once)
#   tools/diskos-dev.sh inputs       fetch + verify the FFmpeg headers and FiiO V2.57, extract the stock sysroot (once)
#   tools/diskos-dev.sh test [args]  host tests: C++ (ctest, ASan/UBSan), legacy ui/tests, font coverage
#   tools/diskos-dev.sh deploy [dir] push ui/build/<dir, default device>/mq_ui to the Disc and hot-reload it
#   tools/diskos-dev.sh capture      build + deploy the player-traffic capture build (ui/build/framelog)
#   tools/diskos-dev.sh mark TEXT    add a note to the capture log ("pressed play"), timed like the frames
#   tools/diskos-dev.sh frames       download the capture log to .dev/captures/
#   tools/diskos-dev.sh ssh [cmd]    shell (or command) on the Disc
#   tools/diskos-dev.sh log          follow the running UI's log (/usr/data/diskos_boot.log)
#   tools/diskos-dev.sh stock        kill mq_ui so the watchdog restores the stock/flashed UI (or just reboot)
#
# Local files live in the git-ignored .dev/: inputs/ (stock firmware and FFmpeg headers, never redistributed),
# build-deps/, captures/ (may hold personal data), and device.env with the Disc's address:
#   DISKOS_IP=192.168.x.y        from Settings > System > Debug Mode
#   DISKOS_PW=...                optional: the Debug Mode password (changes on every enable; prompted if unset)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEV="$ROOT/.dev"
INPUTS="$DEV/inputs"
SYSROOT="$INPUTS/stock-v257"
FFMPEG="$INPUTS/ffmpeg-4.2.2.tar.xz"
FFMPEG_URL=https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.xz
FFMPEG_SHA=cb754255ab0ee2ea5f66f8850e1bd6ad5cac1cd855d0a2f4990fb8c668b0d29c
FW="$INPUTS/SNOWSKY_DISC_update_20260909_v257.zip"   # URL + hash come from firmware_catalogue.py

die() { echo "error: $*" >&2; exit 1; }

in_container() {
  [ -x /opt/mipsel-n2008-musl-cross/bin/mipsel-linux-musl-gcc ] ||
    die "run this in the dev container (docs/DEV_WORKFLOW.md)"
}

device_env() {
  # shellcheck source=/dev/null
  [ -f "$DEV/device.env" ] && . "$DEV/device.env"
  [ -n "${DISKOS_IP:-}" ] || die "set DISKOS_IP in .dev/device.env (Debug Mode shows it)"
  if [ -z "${DISKOS_PW:-}" ]; then
    read -rsp "Debug Mode SSH password for $DISKOS_IP: " DISKOS_PW; echo
  fi
  export DISKOS_IP DISKOS_PW
}

cmd_setup() {
  in_container
  cd "$ROOT"
  uv sync --locked
  uv run --frozen pre-commit install --install-hooks
  # Both builds' compile_commands.json feed clangd (ui/.clangd): host for the tests, device for the rest.
  cd "$ROOT/ui"
  cmake --preset host >/dev/null
  if [ -f "$SYSROOT/lib/libc.so.6" ]; then
    cmake --preset device >/dev/null
  else
    echo ">> no stock inputs yet: run 'tools/diskos-dev.sh inputs', then 'cmake --preset device' in ui/"
  fi
}

cmd_inputs() {
  mkdir -p "$INPUTS"
  if [ ! -f "$FFMPEG" ]; then
    wget -q --show-progress -O "$FFMPEG.part" "$FFMPEG_URL" && mv "$FFMPEG.part" "$FFMPEG"
  fi
  echo "$FFMPEG_SHA  $FFMPEG" | sha256sum -c --quiet || die "FFmpeg archive hash mismatch"
  if [ -f "$SYSROOT/lib/libc.so.6" ]; then echo ">> stock sysroot already present"; return; fi
  # Download (if needed), verify against the catalogue pin, extract + verify the rootfs against PINNED_ROOTFS.
  (cd "$ROOT" && uv run --frozen python - "$FW" "$INPUTS" <<'PY'
import hashlib, os, shutil, sys, urllib.request
from diskos_installer import firmware_catalogue as cat, imagebuild
fw, inputs = sys.argv[1:]
pin = cat.CATALOGUE['257']
if not os.path.exists(fw):
    print('>> downloading', pin.url)
    urllib.request.urlretrieve(pin.url, fw + '.part'); os.replace(fw + '.part', fw)
cat.verify(fw, '257')
out, work = os.path.join(inputs, 'rootfs-v257.squashfs'), os.path.join(inputs, 'work')
os.makedirs(work, exist_ok=True)
imagebuild.extract_stock_rootfs(fw, out, work)
shutil.rmtree(work)
sha, size = imagebuild.PINNED_ROOTFS['257']
h = hashlib.sha256(open(out, 'rb').read()).hexdigest()
if (h, os.path.getsize(out)) != (sha, size):
    sys.exit('extracted rootfs does not match PINNED_ROOTFS[257]')
print('>> rootfs verified against PINNED_ROOTFS[257]')
PY
  )
  # Only the loader and shared libraries are needed (the helper links against them; nothing is shipped).
  unsquashfs -q -n -d "$SYSROOT" "$INPUTS/rootfs-v257.squashfs" lib usr/lib
  for f in lib/ld-linux-mipsn8.so.1 lib/libc.so.6 usr/lib/libavformat.so.58 usr/lib/libavcodec.so.58 usr/lib/libavutil.so.56; do
    [ -e "$SYSROOT/$f" ] || die "stock sysroot is missing $f"
  done
  echo ">> stock sysroot ready: $SYSROOT"
}

# setarch -R turns address-space randomisation off for the sanitized tests: on kernels with wide mmap
# randomisation (WSL, recent Ubuntu) the sanitizers can't start otherwise (ui/src/README.md). The container
# allows it (seccomp=unconfined in devcontainer.json). timeout: never hang.
cmd_test() {
  in_container
  cd "$ROOT/ui"
  local norand=(setarch "$(uname -m)" -R)
  timeout 900 "${norand[@]}" sh -c 'cmake --preset host >/dev/null && cmake --build --preset host && ctest --preset host'
  timeout 900 "${norand[@]}" make -C tests "${@:-run}"
  python3 tests/font_coverage_test.py
}

cmd_deploy() {
  local bin="$ROOT/ui/build/${1:-device}/mq_ui"
  [ -f "$bin" ] || die "no ${bin#"$ROOT/"}; build it first"
  device_env
  "$ROOT/tools/diskos-deploy.sh" "$bin"
}

# Player traffic capture: a DISKOS_FRAME_LOG build in ui/build/framelog, logging to /tmp/diskos_frames.log on the Disc.
cmd_capture() {
  in_container
  (cd "$ROOT/ui" && cmake --preset device -B build/framelog -DDISKOS_FRAME_LOG=ON >/dev/null && cmake --build build/framelog)
  cmd_deploy framelog
}
# A "<uptime> # text" line, on the log's own clock, so it sorts in among the frames.
cmd_mark() {
  local text="$*"
  remote "printf '%s # %s\n' \"\$(cut -d' ' -f1 /proc/uptime)\" '${text//\'/\'\\\'\'}' >> /tmp/diskos_frames.log"
}
cmd_frames() {
  mkdir -p "$DEV/captures"
  local out
  out="$DEV/captures/frames-$(date +%Y%m%d-%H%M%S).log"
  remote 'cat /tmp/diskos_frames.log' > "$out"
  echo "$(wc -l < "$out") lines -> ${out#"$ROOT/"}"
}

# remote [-t] [command...]: run on the Disc as root (no command = interactive shell)
remote() {
  device_env
  local tty=()
  [ "${1:-}" = -t ] && { tty=(-t); shift; }
  SSHPASS="$DISKOS_PW" sshpass -e ssh "${tty[@]}" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 \
    "root@$DISKOS_IP" "$@"
}

case "${1:-}" in
  setup)   cmd_setup ;;
  inputs)  cmd_inputs ;;
  test)    shift; cmd_test "$@" ;;
  deploy)  shift; cmd_deploy "$@" ;;
  capture) cmd_capture ;;
  mark)    shift; cmd_mark "$@" ;;
  frames)  cmd_frames ;;
  ssh)     shift; remote -t "$@" ;;
  log)     remote -t 'tail -n 50 -f /usr/data/diskos_boot.log' ;;
  stock)   remote 'killall mq_ui' ;;
  *) sed -n '2,19p' "$0"; exit 2 ;;
esac
