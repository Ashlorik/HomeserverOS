#!/bin/bash
# Runs the UEFI boot.img from RAM only. Nothing is written to the phone;
# a reboot returns to Evolution X. Phone: hold Vol-Down+Power -> fastboot.
set -euo pipefail
IMG=${1:-$(dirname "$0")/../../out/boot.img}
[ -s "$IMG" ] || { echo "no boot.img: $IMG (build it with lavender/tools/build.sh)"; exit 1; }
fastboot devices | grep -q . || { echo "phone not in fastboot"; exit 1; }
[ "$(fastboot getvar product 2>&1 | grep -o 'lavender')" = lavender ] || { echo "not lavender"; exit 1; }
fastboot boot "$IMG"
