# shellcheck shell=bash
# Optional GPU firmware: copy a selected subset of linux-firmware from this host into /usr/lib/firmware.
# Covers the GPUs this project targets (Intel i915, nouveau GTX 10xx/Pascal and newer, amdgpu/radeon).
# Wi-Fi/Ethernet firmware is handled by the wifi stage; this stage is GPU-focused to avoid overlap.
# WITH_FW=auto|yes|no  (auto = include it when linux-firmware is present on the build host).
# NOTE: firmware goes under usr/lib/firmware because /lib is a symlink to usr/lib in the rootfs.
# NOTE: Ubuntu ships firmware compressed (.zst/.xz); this kernel loads only uncompressed, so we unpack.

WITH_FW=${WITH_FW:-auto}

# GPU firmware directories/globs, relative to the host firmware dir. Kept narrow: the full
# linux-firmware tree is >1 GB; these subsets are tens of MB.
GPU_FW_DIRS=${GPU_FW_DIRS:-"i915 nvidia/gp10* nvidia/gp20* nvidia/tu10* nvidia/tu11* amdgpu radeon"}

stage_firmware(){
  done_p firmware && { log "firmware: done (skipping)"; return 0; }
  local fw=/lib/firmware
  [[ -d $fw ]] || fw=/usr/lib/firmware
  if [[ $WITH_FW == no ]] || { [[ $WITH_FW == auto ]] && [[ ! -d $fw ]]; }; then
    warn "firmware: skipped (WITH_FW=$WITH_FW; linux-firmware on this host: $([[ -d $fw ]] && echo yes || echo no))"
    mark firmware; mkdir -p "$PKG/firmware"; touch "$PKG/firmware/.skipped"; return 0
  fi
  [[ -d $fw ]] || die "WITH_FW=yes but linux-firmware is missing (apt install linux-firmware)"
  log "firmware: collecting GPU firmware from $fw"
  local P=$PKG/firmware dst=$PKG/firmware/usr/lib/firmware d n=0
  rm -rf "$P"; mkdir -p "$dst"
  # getfw NAME (relative to $fw, no compression suffix) -> $dst/NAME, decompressing .zst/.xz. Best effort.
  getfw(){
    local nm=$1 o=$dst/$1; mkdir -p "$(dirname "$o")"
    if [[ -d $fw/$nm ]]; then cp -aL "$fw/$nm" "$(dirname "$o")/" 2>/dev/null && return 0 || return 0
    elif [[ -e $fw/$nm ]]; then cp -L "$fw/$nm" "$o" 2>/dev/null || true
    elif [[ -e $fw/$nm.zst ]]; then zstd -dqf "$fw/$nm.zst" -o "$o" 2>/dev/null || true
    elif [[ -e $fw/$nm.xz ]]; then xz -dc "$fw/$nm.xz" >"$o" 2>/dev/null || true; fi
    return 0; }
  for d in $GPU_FW_DIRS; do
    # expand globs against the host firmware dir
    for m in "$fw"/$d; do [[ -e $m || -e $m.zst || -e $m.xz ]] || continue; getfw "${m#"$fw"/}"; n=$((n+1)); done
  done
  # any directory copied as-is may still contain compressed members: unpack them in place
  find "$dst" -name '*.zst' 2>/dev/null | while read -r z; do zstd -dqf --rm "$z" -o "${z%.zst}" 2>/dev/null || true; done
  find "$dst" -name '*.xz'  2>/dev/null | while read -r z; do xz -df "$z" 2>/dev/null || true; done
  if [[ $n -eq 0 ]] || [[ -z "$(ls -A "$dst" 2>/dev/null)" ]]; then
    warn "firmware: nothing matched in $fw; skipping"
    mark firmware; touch "$P/.skipped"; return 0
  fi
  log "firmware: $(du -sh "$dst" | cut -f1) copied"
  mark firmware
}
