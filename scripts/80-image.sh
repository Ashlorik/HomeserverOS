# shellcheck shell=bash
# Build the bootable disk image without mounting or loop devices:
#   GPT (hs-gpt) -> FAT32 ESP (mkfs.fat --offset + fatput.py) -> ext4 root (mke2fs -d -E offset=)

stage_image(){
  need mke2fs mkfs.fat python3 truncate
  [[ -d $WORK/rootfs/etc ]] || die "run the rootfs stage first"
  ids
  local R=$WORK/rootfs gpt=$PKG/tools/usr/sbin/hs-gpt used_mib root_mib size_mib
  used_mib=$(( $(du -sk "$R" | cut -f1) / 1024 + 1 )); root_mib=$(( used_mib * 118 / 100 + 96 ))
  if [[ $IMG_SIZE == auto ]]; then size_mib=$(( 1 + ESP_MIB + root_mib + 2 )); else size_mib=$(( $(numfmt --from=iec "$IMG_SIZE") / 1048576 )); fi
  log "image: $IMG (${size_mib} MiB: ESP ${ESP_MIB} MiB + root)"
  rm -f "$IMG"; truncate -s "${size_mib}M" "$IMG"
  "$gpt" create "$IMG" --esp-mib "$ESP_MIB" --root-guid "$LIVE_GUID" --esp-guid "$ESP_GUID"
  local l1 l2 o1 o2 s1 s2
  { read -r _ o1 s1 _; read -r _ o2 s2 _; } < <("$gpt" show "$IMG")
  mkfs.fat -F 32 -s 2 -n HSBOOT --offset $(( o1 / 512 )) "$IMG" $(( s1 / 1024 )) >/dev/null
  python3 -I "$SRC_DIR/src/fatput.py" "$IMG" "$o1" "$OUTD/bzImage.live" EFI/BOOT/BOOTX64.EFI
  mke2fs -q -F -t ext4 -L hs-live -d "$R" -E "offset=$o2,root_owner=0:0" "$IMG" $(( s2 / 1024 ))k
  [[ -z ${SUDO_UID:-} ]] || chown "$SUDO_UID:${SUDO_GID:-0}" "$IMG"
  log "image ready: $IMG"
}
