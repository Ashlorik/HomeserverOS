# shellcheck shell=bash
# Wi-Fi: wpa_supplicant + wpa_cli (nl80211) and a curated set of firmware files (newest iwlwifi microcode per chip family, ath9k/ath10k, rtw88/89, rtl8xxxu, brcm, mt76, regulatory database)
WPA_VER=${WPA_VER:-2.11}
FW_DIRS=${FW_DIRS:-"ath10k/QCA6174 ath10k/QCA9984 ath10k/QCA988X ath10k/QCA9377 ath9k_htc rtw88 rtw89 rtlwifi rtl_nic brcm mediatek/mt7601u.bin mediatek/mt7662.bin mediatek/mt7662_rom_patch.bin mediatek/WIFI_RAM_CODE_MT7961_1.bin mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin"}

stage_wifi(){
  done_p wifi && { log "wifi: done (skipping)"; return 0; }
  need gcc make curl pkg-config
  log "wifi: wpa_supplicant $WPA_VER + firmware"
  local P=$PKG/wifi; rm -rf "$P"; mkdir -p "$P/usr/sbin" "$P/usr/bin" "$P/usr/lib" "$P/lib/firmware"
  local tb=$SRC/wpa_supplicant-$WPA_VER.tar.gz
  [[ -f $tb ]] || curl -fL --retry 3 -m 200 -o "$tb" "https://w1.fi/releases/wpa_supplicant-$WPA_VER.tar.gz" || die "wpa_supplicant download failed"
  export WPA_TB=$tb
  sh_step wifi <<'EOF'
rm -rf "$SRC/wpa-b"; mkdir -p "$SRC/wpa-b"; tar -xzf "$WPA_TB" -C "$SRC/wpa-b" --strip-components=1; cd "$SRC/wpa-b/wpa_supplicant"
cat > .config <<'CFG'
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_TLS=openssl
CONFIG_IEEE80211W=y
CONFIG_IEEE80211N=y
CONFIG_IEEE80211AC=y
CONFIG_SAE=y
CONFIG_BACKEND=file
CONFIG_NO_RANDOM_POOL=y
CFG
make -j"$JOBS" wpa_supplicant wpa_cli wpa_passphrase
install -D -m755 wpa_supplicant "$PKG/wifi/usr/sbin/wpa_supplicant"; install -m755 wpa_cli wpa_passphrase "$PKG/wifi/usr/bin/"
EOF
  strip "$P/usr/sbin/wpa_supplicant" "$P/usr/bin/wpa_cli" "$P/usr/bin/wpa_passphrase" 2>/dev/null || true
  ldd "$P/usr/sbin/wpa_supplicant" "$P/usr/bin/wpa_cli" | awk '/=> \//{print $3}' | sort -u | while read -r l; do case $(basename "$l") in libc.so*|libm.so*|libresolv.so*|libdl.so*|libpthread.so*|ld-linux*) ;; *) cp -Ln "$l" "$P/usr/lib/" 2>/dev/null || true;; esac; done
  # firmware (Ubuntu ships .zst files; unpack them). Every copy is best effort: a missing file must not stop the build.
  local fw=/lib/firmware d f
  [[ -d $fw ]] || fw=/usr/lib/firmware
  getfw(){ # NAME (relative to the firmware dir, without compression suffix) -> $P/lib/firmware/NAME
    local n=$1 o=$P/lib/firmware/$1; mkdir -p "$(dirname "$o")"
    if [[ -d $fw/$n ]]; then cp -aL "$fw/$n" "$(dirname "$o")/" 2>/dev/null || true
    elif [[ -e $fw/$n ]]; then cp -L "$fw/$n" "$o" 2>/dev/null || true
    elif [[ -e $fw/$n.zst ]]; then zstd -dqf "$fw/$n.zst" -o "$o" 2>/dev/null || true
    elif [[ -e $fw/$n.xz ]]; then xz -dc "$fw/$n.xz" >"$o" 2>/dev/null || true; fi
    return 0; }
  if [[ -d $fw ]]; then
    ls "$fw" | grep '^iwlwifi-' | sed -E 's/\.(zst|xz)$//' | sort -V | awk '{n=$0; sub(/-[0-9]+\.ucode$/,"",n); last[n]=$0} END{for(k in last) print last[k]}' | while read -r f; do getfw "$f"; done
    for d in $FW_DIRS regulatory.db regulatory.db.p7s; do getfw "$d"; done
    find "$P/lib/firmware" -name '*.zst' | while read -r z; do zstd -dqf --rm "$z" -o "${z%.zst}" 2>/dev/null || true; done
    find "$P/lib/firmware" -name '*.xz' | while read -r z; do xz -df "$z" 2>/dev/null || true; done
  else warn "wifi: no /lib/firmware on the build host (apt install linux-firmware wireless-regdb): Wi-Fi will need hs-firmware later"; fi
  log "wifi: firmware $(du -sh "$P/lib/firmware" | cut -f1), $(ls "$P/lib/firmware" | wc -l) entries"
  mark wifi
}
