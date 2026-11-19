#!/bin/sh
# Build and flash over Wi-Fi. The ESP32 connects back to this host on 3233/tcp to fetch the image.
set -eu
cd "$(dirname "$0")"
target=${1:-imp.local}

[ -f site.h ] || { echo "site.h missing: generate it with homelab's playbooks/imp.yml" >&2; exit 1; }
pass=$(sed -n 's/^#define IMP_OTA_PASS "\(.*\)"$/\1/p' site.h)
[ -n "$pass" ] || { echo "IMP_OTA_PASS not found in site.h" >&2; exit 1; }
core=$(sed -n 's/^ *- platform: esp32:esp32 (\(.*\))$/\1/p' sketch.yaml)
espota="$(arduino-cli config get directories.data)/packages/esp32/hardware/esp32/$core/tools/espota.py"
[ -f "$espota" ] || { echo "espota.py not found for esp32 core $core" >&2; exit 1; }

arduino-cli compile --profile devkit --output-dir build/fw .
python3 "$espota" -i "$target" -p 3232 -P 3233 -a "$pass" -f build/fw/imp.ino.bin -r
