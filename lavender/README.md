# HomeServer OS multiboot for Redmi Note 7 (lavender) — EXPERIMENTAL

Status: scaffold, untested on hardware. Flashes a UEFI (edk2) boot.img with a menu (`boot.cfg`).
Plan: edk2-lavender payload → menu app (buttons/touch) → chainload ARM64 Linux (HomeServer OS, postmarketOS, Debian), raw images (MyOS), Evolution X from backup.

Images: `HSBOOT/<os>/` on /data/media/0, microSD or USB-OTG. Kernel for Linux entries: msm-4.4 (Evolution X) + lavender dtb.

## Safety
1. Zip backs up boot (+dtbo) to /sdcard/HSBOOT/backup with sha256, aborts if device != lavender.
2. Restore: `fastboot flash boot boot.img` or `restore.sh` in OrangeFox terminal.
3. Copy the backup to a PC before flashing.

## Build
`EDK2_FD=<lavender edk2 .fd> lavender/tools/build.sh` → out/boot.img, out/hs-lavender-flash.zip
## TODO
menu EFI app, edk2 build stage in CI, ARM64 HomeServer OS stage, touch/USB-gadget log.

## Launch without overwriting (recommended first test)
`lavender/tools/boot-nowrite.sh [boot.img]` runs `fastboot boot`: the image is loaded into RAM and nothing is flashed. Reboot = back to Evolution X. Needs unlocked bootloader and a PC.
Blocker: boot.img needs the edk2-lavender .fd, which this sandbox cannot download (network restricted). Get a lavender edk2 build, then `EDK2_FD=... lavender/tools/build.sh && lavender/tools/boot-nowrite.sh`.
