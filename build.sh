#!/usr/bin/env bash
# HomeServer OS builder: Linux + BusyBox + TUI tools (dialog, btop, nnn) -> UEFI-bootable .img (GRUB2)
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 1; }
SRC_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
KSERIES=${KSERIES:-6.12} BBVER=${BBVER:-1.37.0} IMG_SIZE=${IMG_SIZE:-500M} ESP_SIZE=${ESP_SIZE:-128M}
NCURSES_REF=${NCURSES_REF:-v6_5_20251227} DIALOG_REF=${DIALOG_REF:-t20260721} BTOP_REF=${BTOP_REF:-v1.3.2} NNN_REF=${NNN_REF:-v5.3}
WORK=$(realpath -m "${WORK:-$PWD/build}") IMG=$(realpath -m "${IMG:-$PWD/homeserver.img}")
KURL=https://cdn.kernel.org/pub/linux/kernel/v${KSERIES%%.*}.x
JOBS=$(nproc) PUUID=$(cat /proc/sys/kernel/random/uuid) LOOP="" MNT="" KIMG="" BBDIR="" STAGE=""
# console look: quiet boot, no logo, dark 16-colour palette (Catppuccin-like)
THEME="quiet logo.nologo vt.default_red=0x1e,0xc4,0x4f,0xc5,0x3b,0x8e,0x3d,0xb4,0x6c,0xf3,0xa6,0xf9,0x89,0xcb,0x94,0xf5 vt.default_grn=0x1e,0x4f,0x9d,0x9d,0x5f,0x5f,0x9a,0xbc,0x70,0x8b,0xe3,0xe2,0xb4,0xa6,0xe2,0xf7 vt.default_blu=0x2e,0x6b,0x69,0x4a,0xa8,0xb0,0xa8,0xe0,0x86,0xa8,0xa1,0xaf,0xfa,0xf7,0xd5,0xff"
CMDLINE="root=PARTUUID=$PUUID rootfstype=ext4 rootwait rw init=/init console=tty0 loglevel=3 $THEME"

log(){ printf '\033[1;32m[+] %s\033[0m\n' "$*"; }
die(){ printf '\033[1;31m[!] %s\033[0m\n' "$*" >&2; exit 1; }
fetch(){ [[ -s $2 ]] || curl -fL --retry 3 -o "$2" "$1"; }
clone(){ [[ -d $WORK/src/$1/.git ]] || GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "$3" "$2" "$WORK/src/$1" 2>/dev/null; }
cleanup(){ local rc=$?; trap - ERR; set +e; sync
  [[ -z $MNT ]] || { umount -R "$MNT" 2>/dev/null; rmdir "$MNT" 2>/dev/null; }
  [[ -z $LOOP ]] || losetup -d "$LOOP"
  exit "$rc"; }
trap cleanup EXIT
trap 'die "failed at line $LINENO"' ERR

preflight(){ local c
  for c in curl git tar make gcc g++ bc flex bison pkg-config sgdisk mkfs.vfat mkfs.ext4 losetup grub-install; do command -v "$c" >/dev/null || die "missing: $c (run prereqs.sh)"; done
  [[ -d $SRC_DIR/overlay ]] || die "overlay/ directory not found next to build.sh"
  [[ -d /usr/lib/locale/C.utf8 ]] || die "host lacks /usr/lib/locale/C.utf8 (needs glibc >= 2.35; Debian 12+/Ubuntu 22.04+)"
  mkdir -p "$WORK/src"; }

build_kernel(){
  local tb on off crit o
  tb=$(curl -fsS "$KURL/" | grep -o "linux-${KSERIES}\.[0-9]*\.tar\.xz" | sort -uV | tail -n1)
  [[ -n $tb ]] || die "kernel lookup failed"
  log "kernel ${tb%.tar.xz}"
  fetch "$KURL/$tb" "$WORK/$tb"
  [[ -d $WORK/${tb%.tar.xz} ]] || tar -xf "$WORK/$tb" -C "$WORK"
  cd "$WORK/${tb%.tar.xz}"
  on=(BLOCK PCI ACPI BLK_DEV_INITRD BINFMT_ELF BINFMT_SCRIPT DEVTMPFS DEVTMPFS_MOUNT TMPFS PROC_FS SYSFS EXT4_FS FAT_FS VFAT_FS NLS_CODEPAGE_437 NLS_ISO8859_1
      EFI EFI_STUB EFI_PARTITION SCSI BLK_DEV_SD ATA SATA_AHCI ATA_PIIX BLK_DEV_NVME MMC MMC_BLOCK MMC_SDHCI MMC_SDHCI_PCI MMC_SDHCI_ACPI
      USB_SUPPORT USB USB_PCI USB_XHCI_HCD USB_XHCI_PCI USB_EHCI_HCD USB_EHCI_PCI USB_OHCI_HCD USB_OHCI_HCD_PCI USB_UHCI_HCD USB_STORAGE USB_UAS
      HID HID_GENERIC USB_HID INPUT_KEYBOARD KEYBOARD_ATKBD VT VT_CONSOLE FB FB_EFI FRAMEBUFFER_CONSOLE FONTS
      NETDEVICES ETHERNET NET_VENDOR_INTEL NET_VENDOR_REALTEK NET_VENDOR_BROADCOM E1000 E1000E IGB IGC R8169 TIGON3 VIRTIO_MENU VIRTIO_PCI VIRTIO_BLK VIRTIO_NET
      RTC_CLASS RTC_DRV_CMOS CMDLINE_BOOL)
  off=(MODULES SOUND KVM DRM WLAN BT MEDIA_SUPPORT INFINIBAND NFC HAMRADIO CAN CMDLINE_OVERRIDE)
  crit=(EXT4_FS VFAT_FS BLK_DEV_SD SATA_AHCI USB_XHCI_HCD USB_EHCI_HCD USB_STORAGE EFI_STUB DEVTMPFS BINFMT_SCRIPT CMDLINE_BOOL FONT_TER16x32 FONT_10x18)
  make -s defconfig
  sed -i -E 's/^(CONFIG_[A-Za-z0-9_]+)=m$/# \1 is not set/' .config
  for o in "${on[@]}"; do scripts/config -e "$o"; done
  for o in "${off[@]}"; do scripts/config -d "$o"; done
  # scripts/config upper-cases names, so mixed-case font symbols are written by hand
  printf 'CONFIG_FONT_8x16=y\nCONFIG_FONT_10x18=y\nCONFIG_FONT_TER16x32=y\n' >>.config
  scripts/config --set-str CMDLINE "$CMDLINE"
  make -s olddefconfig
  for o in "${crit[@]}"; do grep -q "^CONFIG_$o=y" .config || die "CONFIG_$o is not built-in"; done
  make -s -j"$JOBS" bzImage
  KIMG=$PWD/arch/x86/boot/bzImage
  [[ -s $KIMG ]] || die "bzImage missing"
}

build_busybox(){
  local d=busybox-$BBVER
  log "$d"
  fetch "https://busybox.net/downloads/$d.tar.bz2" "$WORK/$d.tar.bz2"
  [[ -d $WORK/$d ]] || tar -xf "$WORK/$d.tar.bz2" -C "$WORK"
  cd "$WORK/$d"
  make -s defconfig
  sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
         -e 's/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' -e 's/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' .config
  make -s oldconfig < <(yes "") >/dev/null
  make -s -j"$JOBS"
  BBDIR=$PWD
}

build_tui(){
  local S=$WORK/src D=$WORK/tui-deps
  STAGE=$WORK/tui-stage
  log "tui: ncurses, dialog, nnn, btop"
  clone ncurses https://github.com/ThomasDickey/ncurses-snapshots "$NCURSES_REF"
  clone dialog  https://github.com/ThomasDickey/dialog-snapshots  "$DIALOG_REF"
  clone nnn     https://github.com/jarun/nnn                      "$NNN_REF"
  clone btop    https://github.com/aristocratos/btop              "$BTOP_REF"
  rm -rf "$D" "$STAGE" "$S/ncurses-b" "$S/dialog-b"; mkdir -p "$D" "$STAGE/usr/bin" "$S/ncurses-b" "$S/dialog-b"
  export PKG_CONFIG_PATH=$D/lib/pkgconfig PKG_CONFIG_LIBDIR=$D/lib/pkgconfig
  cd "$S/ncurses-b"
  "$S/ncurses/configure" --prefix="$D" --without-shared --with-normal --without-debug --without-ada --without-cxx --without-cxx-binding \
    --without-manpages --without-tests --enable-widec --enable-pc-files --with-pkg-config-libdir="$D/lib/pkgconfig" --enable-overwrite \
    --disable-database --with-fallbacks=linux,vt100,vt220,xterm,xterm-256color,screen,screen-256color,tmux,tmux-256color >/dev/null
  make -j"$JOBS" >/dev/null && make install >/dev/null
  cd "$S/dialog-b"
  LDFLAGS="-static -L$D/lib" CPPFLAGS="-I$D/include" "$S/dialog/configure" --prefix="$D" --with-ncursesw --disable-nls --with-pkg-config >/dev/null
  make -j"$JOBS" >/dev/null
  strip -o "$STAGE/usr/bin/dialog" dialog
  cd "$S/nnn"; make clean >/dev/null 2>&1 || true
  make -j"$JOBS" O_NORL=1 O_NOMOUSE=0 STATIC=1 CFLAGS_CURSES="$(pkg-config ncursesw --cflags)" LDLIBS_CURSES="$(pkg-config ncursesw --libs --static)" >/dev/null
  strip -o "$STAGE/usr/bin/nnn" nnn
  cd "$S/btop"; make clean >/dev/null 2>&1 || true
  make -j"$JOBS" STATIC=true GPU_SUPPORT=false >/dev/null
  strip -o "$STAGE/usr/bin/btop" bin/btop
  mkdir -p "$STAGE/usr/share/btop"; cp -a themes "$STAGE/usr/share/btop/"
  unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR
}

make_image(){
  log "image $IMG"
  rm -f "$IMG"; truncate -s "$IMG_SIZE" "$IMG"
  sgdisk -o -n 1:0:+"$ESP_SIZE" -t 1:ef00 -c 1:ESP -n 2:0:0 -t 2:8300 -c 2:rootfs -u 2:"$PUUID" "$IMG" >/dev/null
  LOOP=$(losetup -f --show -P "$IMG")
  udevadm settle 2>/dev/null || true
  local i; for i in {1..20}; do [[ -b ${LOOP}p2 ]] && break; sleep .5; done
  [[ -b ${LOOP}p2 ]] || die "partition nodes missing"
  mkfs.vfat -F32 -n ESP "${LOOP}p1" >/dev/null
  mkfs.ext4 -qF -L rootfs "${LOOP}p2"
  MNT=$(mktemp -d)
  mount "${LOOP}p2" "$MNT"; mkdir -p "$MNT/boot"; mount "${LOOP}p1" "$MNT/boot"
}

make_rootfs(){
  log "rootfs"
  mkdir -p "$MNT"/{proc,sys,dev,run,tmp,root,mnt,home,srv,var/log,var/spool/cron/crontabs,etc/init.d,usr/share/udhcpc,usr/lib/locale}
  chmod 1777 "$MNT/tmp"; chmod 700 "$MNT/root"
  make -C "$BBDIR" -s CONFIG_PREFIX="$MNT" install >/dev/null
  install -m755 "$BBDIR/examples/udhcp/simple.script" "$MNT/usr/share/udhcpc/default.script"
  mknod -m600 "$MNT/dev/console" c 5 1; mknod -m666 "$MNT/dev/null" c 1 3
  ln -sf /proc/mounts "$MNT/etc/mtab"
  install -m755 "$STAGE/usr/bin/"{dialog,nnn,btop} "$MNT/usr/bin/"
  cp -a "$STAGE/usr/share/btop" "$MNT/usr/share/"
  cp -a /usr/lib/locale/C.utf8 "$MNT/usr/lib/locale/"
  cp -a "$SRC_DIR/overlay/." "$MNT/"; chmod 700 "$MNT/root"
  echo "127.0.0.1 localhost" >"$MNT/etc/hosts"
  echo "root:x:0:0:root:/root:/bin/sh" >"$MNT/etc/passwd"
  echo "root:x:0:" >"$MNT/etc/group"
  echo 'root:*::0:99999:7:::' >"$MNT/etc/shadow"; chmod 600 "$MNT/etc/shadow"
  cat >"$MNT/init" <<'EOF'
#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp
[ -x /sbin/init ] && exec /sbin/init
while :; do setsid cttyhack /bin/sh; done
EOF
  chmod 755 "$MNT/init"
}

install_grub(){
  log "grub"
  install -m644 "$KIMG" "$MNT/boot/vmlinuz"
  grub-install --target=x86_64-efi --efi-directory="$MNT/boot" --boot-directory="$MNT/boot" --removable --no-nvram --no-uefi-secure-boot --modules="part_gpt fat ext2" >/dev/null
  cat >"$MNT/boot/grub/grub.cfg" <<EOF
set timeout=2
set default=0
# the kernel command line is built into the kernel; parameters added here are appended to it
menuentry "HomeServer OS" {
  linux /vmlinuz
}
EOF
  [[ -s $MNT/boot/EFI/BOOT/BOOTX64.EFI ]] || die "BOOTX64.EFI missing"
}

finish(){
  sync; umount -R "$MNT"; rmdir "$MNT"; MNT=""
  losetup -d "$LOOP"; LOOP=""
  [[ -z ${SUDO_UID:-} ]] || chown "$SUDO_UID:${SUDO_GID:-0}" "$IMG"
  log "done: $IMG"
}

preflight; build_kernel; build_busybox; build_tui; make_image; make_rootfs; install_grub; finish
