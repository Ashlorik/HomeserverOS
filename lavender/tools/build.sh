#!/bin/bash
# Builds lavender boot.img from edk2 payload (EDK2_FD=path to lavender FD) and a flashable zip.
# Needs: mkbootimg (python), zip. edk2 FD from an edk2-lavender build.
set -euo pipefail
cd "$(dirname "$0")/.."; OUT=../out; mkdir -p $OUT
FD=${EDK2_FD:?set EDK2_FD to the edk2 lavender .fd}
gzip -c "$FD" > $OUT/fd.gz
mkbootimg --kernel $OUT/fd.gz --base 0x0 --kernel_offset 0x00008000 --pagesize 4096 -o $OUT/boot.img
(cd zip && zip -qr ../$OUT/hs-lavender-flash.zip . && cp $OUT/boot.img /tmp/ 2>/dev/null; true)
(cd $OUT && zip -q hs-lavender-flash.zip boot.img)
sha256sum $OUT/boot.img $OUT/hs-lavender-flash.zip
