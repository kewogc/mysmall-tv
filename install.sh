#!/usr/bin/env bash
# Build the firmware (in Docker, via build.sh) and install it on a device over
# WiFi through its web updater (POST /update). Works when the device runs the
# stock GeekMagic firmware or this mod: both take the same multipart upload.
#
#   ./install.sh 192.168.1.50                 build, then install
#   ./install.sh 192.168.1.50 path/to/fw.bin  install an existing OTA image
#
# Environment:
#   PIO_ENV        env to build (default smalltv_esp32_8mb, the SmallTV Pro)
#   SMALLTV_USER   web UI user, if the mod has a password set (default admin)
#   SMALLTV_PASS   web UI password, if the mod has one set
#   ASSUME_YES=1   do not ask before flashing
#
# The SmallTV Pro has no USB-serial chip. If an image does not boot, the only
# way back is a 3.3 V UART adapter on the internal header.
set -euo pipefail

cd "$(dirname "$0")"

host=${1:-}
file=${2:-}
env=${PIO_ENV:-smalltv_esp32_8mb}
# App slot size from partitions/smalltv_8mb_ota.csv (stock SmallTV Pro layout).
slot_size=$((0x220000))

if [ -z "$host" ]; then
  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
fi

auth=()
if [ -n "${SMALLTV_PASS:-}" ]; then
  auth=(--digest -u "${SMALLTV_USER:-admin}:$SMALLTV_PASS")
fi

# Prints what the device says it runs; empty when it does not answer.
device_version() {
  local v
  # Stock firmware.
  v=$(curl -s -m 4 "http://$host/v.json" 2>/dev/null || true)
  if [[ $v == \{*\"v\"* ]]; then
    printf 'stock %s\n' "$v"
    return
  fi
  # This mod.
  v=$(curl -s -m 4 "${auth[@]}" "http://$host/api/status" 2>/dev/null || true)
  if [[ $v == \{* ]]; then
    printf 'mod %s\n' "$(grep -oE '"version" *: *"[^"]*"' <<<"$v" | head -1)"
  fi
}

if [ -z "$file" ]; then
  ./build.sh "$env"
  file="dist/$env/firmware.bin"
fi

[ -f "$file" ] || { echo "No such file: $file" >&2; exit 1; }

# An ESP32 app image starts with 0xE9. This catches a zip, an HTML error page
# or a factory image offset mistake before it reaches the device.
magic=$(head -c 1 "$file" | od -An -tx1 | tr -d ' \n')
if [ "$magic" != "e9" ]; then
  echo "$file does not look like an ESP32 app image (first byte 0x$magic, want 0xe9)" >&2
  exit 1
fi
size=$(stat -c %s "$file")
if [ "$size" -gt "$slot_size" ]; then
  echo "$file is $size bytes, the OTA slot holds $slot_size" >&2
  exit 1
fi

before=$(device_version)
if [ -z "$before" ]; then
  echo "Device at $host does not answer" >&2
  exit 1
fi

echo "Device:  $host  ($before)"
echo "Image:   $file  ($size bytes, md5 $(md5sum "$file" | cut -d' ' -f1))"
if [ "${ASSUME_YES:-}" != 1 ]; then
  read -r -p "Flash it? [y/N] " answer
  [[ $answer == [yY]* ]] || { echo "Cancelled"; exit 1; }
fi

echo "Uploading..."
resp=$(mktemp)
trap 'rm -f "$resp"' EXIT
code=$(curl -sS -m 300 "${auth[@]}" -o "$resp" -w '%{http_code}' \
  -F "firmware=@$file;type=application/octet-stream;filename=firmware.bin" \
  "http://$host/update") || { echo "Upload failed" >&2; exit 1; }
if [ "$code" != 200 ]; then
  echo "Device refused the image: HTTP $code: $(head -c 300 "$resp")" >&2
  exit 1
fi
echo "Accepted: $(head -c 200 "$resp" | sed 's/<[^>]*>//g')"

echo "Waiting for the device to come back..."
sleep 5
for _ in $(seq 1 30); do
  after=$(device_version)
  if [ -n "$after" ]; then
    echo "Device is up: $after"
    exit 0
  fi
  sleep 3
done

cat >&2 <<EOF
Device did not come back on $host within ~95 s.
A first install of the mod over stock firmware does not carry WiFi settings
over: look for the open access point "SmallTV-Setup" and configure WiFi at
http://192.168.4.1/.
EOF
exit 1
