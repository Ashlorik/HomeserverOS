# shellcheck shell=bash
# Static BusyBox: the base userspace.

stage_busybox(){
  done_p busybox && { log "busybox: done (skipping)"; return 0; }
  need curl git make gcc tar bzip2
  local d=busybox-$BBVER
  log "busybox $BBVER"
  if [[ ! -d $SRC/$d ]]; then
    if curl -fL --retry 2 -m 120 -o "$SRC/$d.tar.bz2" "https://busybox.net/downloads/$d.tar.bz2" 2>/dev/null; then
      tar -xf "$SRC/$d.tar.bz2" -C "$SRC"
    else
      log "busybox source: GitHub mirror (busybox.net not reachable)"
      gitsrc "$d" https://github.com/mirror/busybox "${BBVER//./_}"
    fi
  fi
  export BBSRC=$SRC/$d
  sh_step busybox <<'EOF'
cd "$BBSRC"
make -s defconfig
sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
       -e 's/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' -e 's/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' .config
make -s oldconfig </dev/null >/dev/null 2>&1 || true
make -s -j"$JOBS"
for a in blkid blockdev cryptpw depmod modprobe lsmod mkfs.vfat chpasswd start-stop-daemon httpd ntpd crond udhcpc ssl_client; do
  ./busybox --list | grep -qx "$a" || { echo "BusyBox applet missing: $a"; exit 1; }
done
EOF
  echo "$SRC/$d" >"$OUTD/busybox.dir"
  mark busybox
}
