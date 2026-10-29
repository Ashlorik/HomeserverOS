# shellcheck shell=bash
# Assemble the root filesystem tree in $WORK/rootfs (no mounting needed)

stage_rootfs(){
  local s; for s in kernel busybox tui e2fs dropbear curl browser tools gl firmware; do done_p "$s" || die "stage '$s' has not run yet (run: ./build.sh $s)"; done
  ids
  local R=$WORK/rootfs bb rel; bb=$(cat "$OUTD/busybox.dir"); rel=$(cat "$OUTD/kernel.release")
  log "rootfs: assembling"
  rm -rf "$R"; mkdir -p "$R"/{proc,sys,dev,run,tmp,root,mnt,home,srv,boot,var/log,var/spool/cron/crontabs,etc/init.d,etc/ssl/certs,usr/share/udhcpc,usr/share/hs,usr/lib/locale,usr/lib64}
  ln -s usr/lib "$R/lib"; ln -s usr/lib64 "$R/lib64"
  chmod 1777 "$R/tmp"; chmod 700 "$R/root"
  make -C "$bb" -s CONFIG_PREFIX="$R" install >/dev/null
  install -m755 "$bb/examples/udhcp/simple.script" "$R/usr/share/udhcpc/default.script"
  mknod -m600 "$R/dev/console" c 5 1 2>/dev/null || warn "cannot create /dev/console (devtmpfs provides it at boot)"
  mknod -m666 "$R/dev/null" c 1 3 2>/dev/null || true
  local p; for p in tui e2fs dropbear curl browser tools gl firmware; do [[ -f $PKG/$p/.skipped || ( $p == gl && ${LITE:-0} == 1 ) || ( $p == firmware && ${LITE:-0} == 1 ) ]] && continue; cp -a "$PKG/$p/." "$R/"; rm -f "$R/.done" "$R/.skipped"; done
  ln -sf dropbearmulti "$R/usr/bin/dropbear"; ln -sf dropbearmulti "$R/usr/bin/dropbearkey"; ln -sf dropbearmulti "$R/usr/bin/dbclient"; ln -sf dropbearmulti "$R/usr/bin/scp"
  local t; for t in mke2fs e2fsck tune2fs resize2fs mkfs.ext2 mkfs.ext3 mkfs.ext4 fsck.ext2 fsck.ext3 fsck.ext4; do rm -f "$R"/{bin,sbin,usr/bin}/"$t"; done   # the full e2fsprogs replace BusyBox's
  for t in mkfs.ext2 mkfs.ext3 mkfs.ext4; do ln -s mke2fs "$R/usr/sbin/$t"; done; for t in fsck.ext2 fsck.ext3 fsck.ext4; do ln -s e2fsck "$R/usr/sbin/$t"; done
  [[ -f $PKG/gl/.skipped || ${LITE:-0} == 1 ]] && rm -f "$R/usr/bin/hs-glinfo.bin"
  mkdir -p "$R/usr/lib/modules"; cp -a "$PKG/kmods/lib/modules/." "$R/usr/lib/modules/"
  [[ -f $R/usr/lib/modules/$rel/modules.dep ]] || chroot "$R" /bin/busybox depmod "$rel" || warn "depmod failed"   # BusyBox writes modules.dep.bb
  [[ -f $R/usr/lib/modules/$rel/modules.dep || -f $R/usr/lib/modules/$rel/modules.dep.bb ]] || warn "no module dependency file: GPU modules will not auto-load"
  cp -a /usr/lib/locale/C.utf8 "$R/usr/lib/locale/"
  # CA bundle: only the public Mozilla list (never any CA that this build host trusts privately)
  local cad=/usr/share/ca-certificates/mozilla
  [[ -d $cad ]] || die "$cad missing (apt install ca-certificates)"
  cat "$cad"/*.crt >"$R/etc/ssl/certs/ca-certificates.crt"
  [[ $(grep -c 'BEGIN CERTIFICATE' "$R/etc/ssl/certs/ca-certificates.crt") -gt 100 ]] || die "CA bundle looks too small"
  cp -a "$SRC_DIR/overlay/." "$R/"; chmod 700 "$R/root"
  echo "127.0.0.1 localhost" >"$R/etc/hosts"
  # accounts: root (administration through the menu) + admin (daily user, SSH, downloaded programs); both locked until the wizard sets a password
  printf 'root:x:0:0:root:/root:/bin/sh\nadmin:x:1000:1000:Admin:/home/admin:/bin/sh\n' >"$R/etc/passwd"
  printf 'root:x:0:\nadmin:x:1000:\n' >"$R/etc/group"
  printf 'root:*::0:99999:7:::\nadmin:*::0:99999:7:::\n' >"$R/etc/shadow"; chmod 600 "$R/etc/shadow"
  mkdir -p "$R/home/admin/.ssh" "$R/opt/apps" "$R/usr/local/bin"; chown -R 1000:1000 "$R/home/admin"; chmod 700 "$R/home/admin" "$R/home/admin/.ssh"
  # su/passwd must work for ordinary users: BusyBox is setuid root, but only these applets keep root (see busybox.conf)
  chmod 4755 "$R/bin/busybox"; printf '[SUID]\nsu = ssx root.root\npasswd = ssx root.root\n' >"$R/etc/busybox.conf"; chmod 644 "$R/etc/busybox.conf"
  # glibc compatibility layer (small): lets downloaded programs such as fastfetch run
  local L=/usr/lib/x86_64-linux-gnu g; mkdir -p "$R$L" "$R/usr/lib64"
  for g in libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libresolv.so.2 libgcc_s.so.1 libstdc++.so.6; do [[ -e $L/$g ]] && cp -L "$L/$g" "$R$L/$g"; done
  cp -L /lib64/ld-linux-x86-64.so.2 "$R/usr/lib64/"
  printf '/usr/lib/x86_64-linux-gnu\n/usr/local/lib\n' >"$R/etc/ld.so.conf"; ldconfig -r "$R" || warn "ldconfig failed (downloaded programs may not find libraries)"
  ln -sf /proc/mounts "$R/etc/mtab"
  cat >"$R/init" <<'EOT'
#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp
[ -x /sbin/init ] && exec /sbin/init
while :; do setsid cttyhack /bin/sh; done
EOT
  chmod 755 "$R/init"
  # live medium specifics: marker + the kernel that the installer puts on the target disk
  mkdir -p "$R/etc/hs"; : >"$R/etc/hs/live"
  install -m644 "$OUTD/bzImage.inst" "$R/usr/share/hs/vmlinuz-installed"
  printf 'INST_PARTUUID=%s\nESP_PARTUUID=%s\nESP_MIB=%s\n' "$INST_GUID" "$ESP_GUID" "$ESP_MIB" >"$R/usr/share/hs/install.conf"
  log "rootfs: $(du -sh "$R" | cut -f1)"
}
