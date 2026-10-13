# homeserver-v3-lite.img.xz

21 MB compressed, 290 MiB raw ("lite": no 3D runtime; the full image is built with `./build.sh`). GPT: 128 MiB FAT32 ESP + ext4 root. Linux 6.12.112, static BusyBox 1.36.1, Dropbear (SSH), Lynx, curl. UEFI only, Secure Boot off. Unpacking needs ~260 MB of RAM (xz dictionary).

```
xz -dc homeserver-v3-lite.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```
Boot -> "Install to disk" (or try live). SHA-256: see SHA256SUMS. Not boot-tested on hardware/QEMU; see README.md (Limits).

Образ: 21 МБ (распакованный 290 МиБ), вариант "lite" без 3D-среды. Запись на флешку — командой выше или Rufus/Etcher (DD). Только UEFI, Secure Boot выключить. Подробности и ограничения — в README.md.
