# shellcheck shell=bash
# Hybrid ISO (BIOS: isolinux, UEFI: El Torito FAT image; also writable to a USB stick).
# The system runs from RAM-backed overlay: rootfs.img (ext4, read-only, loop) + tmpfs. A small initramfs (embedded in a third kernel) finds the ISO by itself,
# so there is no PARTUUID and the medium works on any computer.
ISOLINUX_BIN=${ISOLINUX_BIN:-/usr/lib/ISOLINUX/isolinux.bin}; ISOHDPFX=${ISOHDPFX:-/usr/lib/ISOLINUX/isohdpfx.bin}

stage_iso(){
  need mke2fs xorriso cpio python3 mkfs.fat
  [[ -e $ISOLINUX_BIN && -e $ISOHDPFX && -e $SYSLINUX_MODS/ldlinux.c32 ]] || die "isolinux files missing (apt install isolinux syslinux-common)"
  [[ -d $WORK/rootfs/etc ]] || die "run the rootfs stage first"
  local R=$WORK/rootfs KO=$WORK/kbuild I=$WORK/iso bb; bb=$(cat "$OUTD/busybox.dir")/busybox
  rm -rf "$I" "$WORK/initramfs"; mkdir -p "$I/boot" "$I/hs" "$I/isolinux" "$I/EFI" "$WORK/initramfs"/{bin,dev,proc,sys,mnt,ro,rw,newroot,run}
  log "iso: initramfs"
  install -m755 "$bb" "$WORK/initramfs/bin/busybox"
  for a in sh mount umount mkdir sleep switch_root losetup cat grep sed ls mknod echo dmesg; do ln -sf busybox "$WORK/initramfs/bin/$a"; done
  cat >"$WORK/initramfs/init" <<'INIT'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev 2>/dev/null
echo "HomeServer OS: looking for the boot medium..." >/dev/console 2>&1
# options from the kernel command line: hs.safe=1 is handled later by rcS, init=... is passed on
init=/init
for w in $(cat /proc/cmdline); do case $w in init=/init) ;; init=*) init=${w#init=};; esac; done
found=
i=0
while [ $i -lt 40 ] && [ -z "$found" ]; do
  for d in /dev/sr* /dev/sd* /dev/vd* /dev/nvme*n*p* /dev/nvme*n* /dev/mmcblk*; do
    [ -b "$d" ] || continue
    mount -t iso9660 -o ro "$d" /mnt 2>/dev/null || continue
    if [ -f /mnt/hs/rootfs.img ]; then found=$d; break; fi
    umount /mnt
  done
  [ -n "$found" ] || { sleep 1; i=$((i+1)); }
done
if [ -z "$found" ]; then echo "HomeServer OS: boot medium not found - starting a shell" >/dev/console 2>&1; exec sh </dev/console >/dev/console 2>&1; fi
echo "HomeServer OS: medium $found" >/dev/console 2>&1
losetup -r /dev/loop0 /mnt/hs/rootfs.img || losetup -r -f /mnt/hs/rootfs.img
mount -t ext4 -o ro /dev/loop0 /ro || { echo "cannot mount rootfs" >/dev/console; exec sh </dev/console >/dev/console 2>&1; }
mount -t tmpfs -o size=75% tmpfs /rw; mkdir -p /rw/up /rw/work
mount -t overlay overlay -o lowerdir=/ro,upperdir=/rw/up,workdir=/rw/work /newroot
mkdir -p /newroot/mnt/medium 2>/dev/null
mount --move /mnt /newroot/mnt/medium 2>/dev/null
mount --move /dev /newroot/dev 2>/dev/null
exec switch_root /newroot "$init"
INIT
  chmod 755 "$WORK/initramfs/init"; mknod -m600 "$WORK/initramfs/dev/console" c 5 1 2>/dev/null || true
  ( cd "$WORK/initramfs" && find . | cpio -o -H newc --quiet ) >"$WORK/initramfs.cpio"

  log "iso: kernel with built-in initramfs"
  C(){ "$WORK/linux/scripts/config" --file "$KO/.config" "$@"; }
  C --set-str INITRAMFS_SOURCE "$WORK/initramfs.cpio"
  C --set-str CMDLINE "rootwait rw init=/init console=ttyS0,115200 console=tty0 loglevel=3 $THEME_ARGS hs.iso=1"
  make -C "$WORK/linux" O="$KO" -s olddefconfig
  make -C "$WORK/linux" O="$KO" -s -j"$JOBS" bzImage >>"$LOGS/kernel.log" 2>&1 || { tail -n 30 "$LOGS/kernel.log" >&2; die "iso kernel build failed"; }
  cp "$KO/arch/x86/boot/bzImage" "$OUTD/bzImage.iso"
  # leave the tree as the other stages expect it (no embedded initramfs)
  C --set-str INITRAMFS_SOURCE ""

  log "iso: root image"
  local used_mib; used_mib=$(( $(du -sk "$R" | cut -f1) / 1024 + 1 ))
  rm -f "$I/hs/rootfs.img"; truncate -s "$(( used_mib * 110 / 100 + 32 ))M" "$I/hs/rootfs.img"
  mke2fs -q -F -t ext4 -O ^has_journal -L hs-live -d "$R" -E root_owner=0:0 "$I/hs/rootfs.img"

  # UEFI: FAT image with the kernel as the removable-media loader
  local efi=$I/efi.img; truncate -s 40M "$efi"
  mkfs.fat -F 32 -s 1 -n HSEFI "$efi" >/dev/null
  python3 -I "$SRC_DIR/src/fatput.py" "$efi" 0 "$OUTD/bzImage.iso" EFI/BOOT/BOOTX64.EFI 2>/dev/null || die "fatput into efi.img failed"
  rmdir "$I/EFI"
  # BIOS: isolinux
  cp "$OUTD/bzImage.iso" "$I/boot/vmlinuz"
  cp "$ISOLINUX_BIN" "$I/isolinux/"; for m in ldlinux libcom32 libutil menu; do cp "$SYSLINUX_MODS/$m.c32" "$I/isolinux/"; done
  cat >"$I/isolinux/isolinux.cfg" <<'CFG'
UI menu.c32
MENU TITLE HomeServer OS
PROMPT 0
TIMEOUT 50
DEFAULT hs
LABEL hs
  MENU LABEL HomeServer OS (live, runs from RAM)
  LINUX /boot/vmlinuz
LABEL safe
  MENU LABEL Safe mode (no video drivers, no power tuning, no auto services)
  LINUX /boot/vmlinuz
  APPEND hs.safe=1
LABEL rescue
  MENU LABEL Recovery shell (init=/bin/sh)
  LINUX /boot/vmlinuz
  APPEND init=/bin/sh
CFG
  local iso=${ISO:-$OUT/homeserver.iso}; mkdir -p "$(dirname "$iso")"; rm -f "$iso"
  xorriso -as mkisofs -quiet -o "$iso" -V HSLIVE -J -R \
    -isohybrid-mbr "$ISOHDPFX" -c isolinux/boot.cat -b isolinux/isolinux.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
    -eltorito-alt-boot -e efi.img -no-emul-boot -isohybrid-gpt-basdat "$I" || die "xorriso failed"
  [[ -z ${SUDO_UID:-} ]] || chown "$SUDO_UID:${SUDO_GID:-0}" "$iso"
  log "iso ready: $iso ($(du -h "$iso" | cut -f1))"
}
