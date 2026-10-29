# shellcheck shell=bash
# Optional device firmware: copy a selected subset of linux-firmware from this host into /lib/firmware.
# Covers the GPUs this project targets (Intel i915, nouveau GTX 10xx/Pascal, amdgpu) and common Wi-Fi/Ethernet.
# WITH_FW=auto|yes|no  (auto = include it when linux-firmware is present on the build host).

FW_SRC=${FW_SRC:-/lib/firmware}
WITH_FW=${WITH_FW:-auto}

# Firmware directories/globs to copy, relative to $FW_SRC. Kept narrow on purpose: the full
# linux-firmware tree is >1 GB, these subsets are tens of MB.
fw_globs(){ cat <<'EOF'
i915
nvidia/gp10*
nvidia/gp20*
nvidia/tu10*
amdgpu
radeon
regulatory.db
regulatory.db.p7s
rtl_nic
rtlwifi
rtw88
rtw89
iwlwifi-*
ath10k
ath11k
ath9k_htc
mediatek/mt7*
intel/ibt-*
EOF
}

fw_host_ok(){ [[ -d $FW_SRC/i915 || -d $FW_SRC/amdgpu || -e $FW_SRC/regulatory.db ]]; }

stage_firmware(){
  done_p firmware && { log "firmware: done (skipping)"; return 0; }
  if [[ $WITH_FW == no ]] || { [[ $WITH_FW == auto ]] && ! fw_host_ok; }; then
    warn "firmware: skipped (WITH_FW=$WITH_FW; linux-firmware on this host: $(fw_host_ok && echo yes || echo no))"
    mark firmware; mkdir -p "$PKG/firmware"; touch "$PKG/firmware/.skipped"; return 0
  fi
  fw_host_ok || die "WITH_FW=yes but linux-firmware is missing (apt install linux-firmware)"
  log "firmware: collecting a subset of linux-firmware from $FW_SRC"
  local P=$PKG/firmware dst=$PKG/firmware/lib/firmware g n=0
  rm -rf "$P"; mkdir -p "$dst"
  local g m rel
  while read -r g; do
    [[ -n $g ]] || continue
    for m in "$FW_SRC"/$g; do
      [[ -e $m ]] || continue
      rel=${m#"$FW_SRC"/}
      mkdir -p "$dst/$(dirname "$rel")"
      # -L dereferences symlinks (linux-firmware uses many) so the image needs no out-of-tree targets
      cp -aL "$m" "$dst/$(dirname "$rel")/" 2>/dev/null && n=$((n+1)) || true
    done
  done < <(fw_globs)
  [[ $n -gt 0 ]] || { warn "firmware: nothing matched; skipping"; mark firmware; mkdir -p "$P"; touch "$P/.skipped"; return 0; }
  log "firmware: $(du -sh "$dst" | cut -f1) copied ($n items)"
  mark firmware
}
