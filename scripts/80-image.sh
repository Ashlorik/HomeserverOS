# shellcheck shell=bash
# Build the bootable disk image without mounting or loop devices:
#   GPT (hs-gpt) -> FAT32 ESP (mkfs.fat --offset + fatput.py) -> ext4 root (mke2fs -d -E offset=)

# BIOS boot (syslinux on the FAT partition, gptmbr.bin in the protective MBR) for the disk image IMG whose ESP starts at byte OFF.
# The same kernel file serves UEFI (EFI/BOOT/BOOTX64.EFI) and BIOS (VMLINUZ). The kernel's built-in command line carries root=PARTUUID.
SYSLINUX_MODS=${SYSLINUX_MODS:-/usr/lib/syslinux/modules/bios}; GPTMBR=${GPTMBR:-/usr/lib/syslinux/mbr/gptmbr.bin}
bios_boot(){ # IMG OFFSET KERNEL
  local img=$1 off=$2 kern=$3 cfg=$WORK/syslinux.cfg
  # boot menu (menu.c32): normal start after 3 s, safe mode, recovery shell; the kernel command line (root=PARTUUID, console) is built in
  cat >"$cfg" <<'CFG'
UI menu.c32
MENU TITLE HomeServer OS
PROMPT 0
TIMEOUT 30
DEFAULT hs

LABEL hs
  MENU LABEL HomeServer OS
  LINUX /VMLINUZ

LABEL safe
  MENU LABEL Safe mode (no video drivers, no power tuning, no auto services)
  LINUX /VMLINUZ
  APPEND hs.safe=1

LABEL rescue
  MENU LABEL Recovery shell (init=/bin/sh)
  LINUX /VMLINUZ
  APPEND init=/bin/sh
CFG
  python3 -I "$SRC_DIR/src/fatput.py" "$img" "$off" "$kern" VMLINUZ
  python3 -I "$SRC_DIR/src/fatput.py" "$img" "$off" "$cfg" SYSLINUX.CFG
  local m; for m in ldlinux libcom32 libutil menu; do
    python3 -I "$SRC_DIR/src/fatput.py" "$img" "$off" "$SYSLINUX_MODS/$m.c32" "$(echo "$m" | tr a-z A-Z).C32"; done
  syslinux --offset "$off" --install "$img" || die "syslinux --install failed"
  dd if="$GPTMBR" of="$img" bs=440 count=1 conv=notrunc status=none || die "cannot write the MBR boot code"
}

stage_image(){
  need mke2fs mkfs.fat python3 truncate syslinux xz
  [[ -e $SYSLINUX_MODS/ldlinux.c32 && -e $GPTMBR ]] || die "syslinux files missing (apt install syslinux syslinux-common)"
  [[ -d $WORK/rootfs/etc ]] || die "run the rootfs stage first"
  ids
  local R=$WORK/rootfs gpt=$PKG/tools/usr/sbin/hs-gpt used_mib root_mib size_mib
  used_mib=$(( $(du -sk "$R" | cut -f1) / 1024 + 1 )); root_mib=$(( used_mib * 118 / 100 + 96 ))
  if [[ $IMG_SIZE == auto ]]; then size_mib=$(( 1 + ESP_MIB + root_mib + 2 )); else size_mib=$(( $(numfmt --from=iec "$IMG_SIZE") / 1048576 )); fi
  log "image: $IMG (${size_mib} MiB: ESP ${ESP_MIB} MiB + root)"
  mkdir -p "$(dirname "$IMG")"; rm -f "$IMG"; truncate -s "${size_mib}M" "$IMG"
  # ESP image for the installer: same layout, installed kernel, BIOS+UEFI loaders; the installer writes it straight to the new disk at 1 MiB
  local tmp=$WORK/esp-tmp.img
  rm -f "$tmp"; truncate -s "$(( 1 + ESP_MIB + 70 ))M" "$tmp"
  "$gpt" create "$tmp" --esp-mib "$ESP_MIB" --root-guid "$INST_GUID" --esp-guid "$ESP_GUID" --bios
  local t1 t2 t3 t4; { read -r _ t1 t2 _; } < <("$gpt" show "$tmp")
  mkfs.fat -F 32 -s 2 -n HSBOOT -h $(( t1 / 512 )) --offset $(( t1 / 512 )) "$tmp" $(( t2 / 1024 )) >/dev/null
  python3 -I "$SRC_DIR/src/fatput.py" "$tmp" "$t1" "$OUTD/bzImage.inst" EFI/BOOT/BOOTX64.EFI
  bios_boot "$tmp" "$t1" "$OUTD/bzImage.inst"
  dd if="$tmp" of="$WORK/esp-installed.img" bs=1M skip=1 count="$ESP_MIB" status=none
  xz -T1 -6 -f -c "$WORK/esp-installed.img" >"$R/usr/share/hs/esp-installed.img.xz"
  install -m644 "$GPTMBR" "$R/usr/share/hs/gptmbr.bin"; rm -f "$tmp" "$WORK/esp-installed.img"
  "$gpt" create "$IMG" --esp-mib "$ESP_MIB" --root-guid "$LIVE_GUID" --esp-guid "$ESP_GUID" --bios
  local l1 l2 o1 o2 s1 s2
  { read -r _ o1 s1 _; read -r _ o2 s2 _; } < <("$gpt" show "$IMG")
  mkfs.fat -F 32 -s 2 -n HSBOOT -h $(( o1 / 512 )) --offset $(( o1 / 512 )) "$IMG" $(( s1 / 1024 )) >/dev/null
  python3 -I "$SRC_DIR/src/fatput.py" "$IMG" "$o1" "$OUTD/bzImage.live" EFI/BOOT/BOOTX64.EFI
  bios_boot "$IMG" "$o1" "$OUTD/bzImage.live"
  mke2fs -q -F -t ext4 -L hs-live -d "$R" -E "offset=$o2,root_owner=0:0" "$IMG" $(( s2 / 1024 ))k
  [[ -z ${SUDO_UID:-} ]] || chown "$SUDO_UID:${SUDO_GID:-0}" "$IMG"
  log "image ready: $IMG"
}
