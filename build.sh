#!/usr/bin/env bash
# Build the firmware in Docker. Nothing is installed on the host: PlatformIO
# lives in the image, its downloads live in a cache directory.
#
#   ./build.sh                    build the firmware (GeekMagic SmallTV Pro)
#   ./build.sh clean [env]        remove build output for an env
#   ./build.sh pio <args...>      run any PlatformIO command in the container
#
# Result: dist/<env>/firmware.bin          upload through the device web UI (OTA)
#         dist/<env>/firmware.factory.bin  full image for esptool at 0x0
#
# Environment:
#   PIO_CACHE_DIR  where PlatformIO keeps toolchains (default ~/.cache/pio-docker)
#   PIO_IMAGE      image tag (default smalltv-pio)
set -euo pipefail

cd "$(dirname "$0")"
ROOT=$PWD
DEFAULT_ENV=smalltv_esp32_8mb
IMAGE=${PIO_IMAGE:-smalltv-pio}
CACHE=${PIO_CACHE_DIR:-$HOME/.cache/pio-docker}

mkdir -p "$CACHE"
# Cheap when nothing changed: Docker reuses the cached layers.
docker build -q -t "$IMAGE" docker >/dev/null

in_container() {
  local tty=()
  [ -t 1 ] && tty=(-t)
  docker run --rm "${tty[@]}" \
    -u "$(id -u):$(id -g)" \
    -v "$CACHE":/cache \
    -v "$ROOT":/src \
    "$IMAGE" "$@"
}

case "${1:-}" in
  pio)
    shift
    in_container pio "$@"
    ;;
  clean)
    in_container pio run -e "${2:-$DEFAULT_ENV}" -t clean
    rm -rf "dist/${2:-$DEFAULT_ENV}"
    ;;
  *)
    env=${1:-$DEFAULT_ENV}
    # src/webui.h is the gzipped src/webui.html; keep it in step with the source.
    in_container python tools/gzip_webui.py
    in_container pio run -e "$env"
    out="dist/$env"
    mkdir -p "$out"
    for f in firmware.bin firmware.factory.bin; do
      [ -f ".pio/build/$env/$f" ] && cp ".pio/build/$env/$f" "$out/$f"
    done
    echo
    echo "Built $env:"
    (cd "$out" && ls -l && md5sum -- *.bin)
    ;;
esac
