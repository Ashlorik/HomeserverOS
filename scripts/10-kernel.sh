# shellcheck shell=bash
# Linux kernel: storage/USB/network drivers built in, GPU drivers as modules.
# Two images are produced: the "live" kernel (root = live medium) and the "installed" kernel
# (root = the partition created by the installer). They differ only in the built-in command line.

kernel_fetch(){
  [[ -f $WORK/linux/Makefile ]] && return 0
  local kurl="https://cdn.kernel.org/pub/linux/kernel/v${KSERIES%%.*}.x" tb tag
  tb=$(curl -fsS -m 30 "$kurl/" 2>/dev/null | grep -o "linux-${KSERIES}\.[0-9]*\.tar\.xz" | sort -uV | tail -n1 || true)
  if [[ -n $tb ]] && curl -fL --retry 3 -m 900 -o "$SRC/$tb" "$kurl/$tb" 2>/dev/null; then
    log "kernel source: kernel.org ($tb)"
    rm -rf "$WORK/linux"; mkdir -p "$WORK/linux"; tar -xf "$SRC/$tb" -C "$WORK/linux" --strip-components=1
  else
    log "kernel source: GitHub mirror (kernel.org not reachable)"
    tag=$(git ls-remote --tags --refs https://github.com/gregkh/linux "v${KSERIES}.*" | sed 's#.*refs/tags/##' | sort -V | tail -n1)
    [[ -n $tag ]] || die "cannot find a v${KSERIES}.x kernel tag"
    rm -rf "$WORK/linux"
    GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "$tag" https://github.com/gregkh/linux "$WORK/linux" 2>/dev/null || true
    [[ -f $WORK/linux/Makefile ]] || die "kernel clone failed"
  fi
}

stage_kernel(){
  done_p kernel && { log "kernel: done (skipping)"; return 0; }
  need curl git make gcc bc flex bison
  ids; kernel_fetch
  local KO=$WORK/kbuild o
  if [[ ! -f /usr/include/gelf.h && ! -f $DEPS/include/gelf.h ]]; then   # libelf-dev missing: build a tiny libelf
    log "kernel: building libelf (libelf-dev not installed)"
    gitsrc libelf https://github.com/arachsys/libelf master
    sh_step libelf <<'EOF2'
cd "$SRC/libelf"; sed -i 's/ -lzstd//' Makefile; sed -i '/USE_ZSTD/d' src/config.h
make -j"$JOBS"; make install-static PREFIX="$DEPS"
mkdir -p "$DEPS/lib/pkgconfig"
printf "prefix=$DEPS\nName: libelf\nDescription: libelf\nVersion: 0.191\nCflags: -I\${prefix}/include\nLibs: -L\${prefix}/lib -lelf -lz\n" >"$DEPS/lib/pkgconfig/libelf.pc"
EOF2
  fi
  [[ -f $DEPS/include/gelf.h ]] && export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig:${PKG_CONFIG_PATH:-}" HOSTCFLAGS="-I$DEPS/include" HOSTLDFLAGS="-L$DEPS/lib"
  mkdir -p "$KO"
  M(){ make -C "$WORK/linux" O="$KO" "$@"; }
  C(){ "$WORK/linux/scripts/config" --file "$KO/.config" "$@"; }

  local builtin=(BLOCK PCI ACPI BLK_DEV_INITRD BINFMT_ELF BINFMT_SCRIPT DEVTMPFS DEVTMPFS_MOUNT TMPFS PROC_FS SYSFS
    EXT4_FS ISO9660_FS JOLIET BLK_DEV_SR BLK_DEV_LOOP OVERLAY_FS FAT_FS VFAT_FS NTFS3_FS EXFAT_FS NLS_CODEPAGE_437 NLS_ISO8859_1 NLS_UTF8
    EFI EFI_STUB EFI_PARTITION SCSI BLK_DEV_SD ATA SATA_AHCI ATA_PIIX BLK_DEV_NVME
    MMC MMC_BLOCK MMC_SDHCI MMC_SDHCI_PCI MMC_SDHCI_ACPI
    USB_SUPPORT USB USB_PCI USB_XHCI_HCD USB_XHCI_PCI USB_EHCI_HCD USB_EHCI_PCI USB_OHCI_HCD USB_OHCI_HCD_PCI USB_UHCI_HCD USB_STORAGE USB_UAS
    HID HID_GENERIC USB_HID INPUT_KEYBOARD KEYBOARD_ATKBD VT VT_CONSOLE FB FB_EFI FRAMEBUFFER_CONSOLE FONTS
    NETDEVICES ETHERNET NET_VENDOR_INTEL NET_VENDOR_REALTEK NET_VENDOR_BROADCOM E1000 E1000E IGB IGC R8169 TIGON3
    VIRTIO_MENU VIRTIO_PCI VIRTIO_BLK VIRTIO_NET RTC_CLASS RTC_DRV_CMOS UNIX98_PTYS
    HWMON SENSORS_CORETEMP SENSORS_K10TEMP
    MODULES MODULE_UNLOAD CMDLINE_BOOL
    SYSFB_SIMPLEFB DRM DRM_KMS_HELPER DRM_FBDEV_EMULATION DRM_SIMPLEDRM)
  # laptop / power / touchpad: checked softly (warn, never fatal)
  local laptop=(ACPI_BATTERY ACPI_AC ACPI_BUTTON ACPI_VIDEO ACPI_FAN ACPI_THERMAL THERMAL X86_PKG_TEMP_THERMAL
    INPUT_EVDEV INPUT_MOUSEDEV INPUT_MOUSE MOUSE_PS2 MOUSE_PS2_SYNAPTICS MOUSE_PS2_ELANTECH MOUSE_PS2_TRACKPOINT
    I2C I2C_HID I2C_HID_ACPI HID_MULTITOUCH HID_GENERIC I2C_DESIGNWARE_CORE I2C_DESIGNWARE_PLATFORM MFD_INTEL_LPSS_PCI MFD_INTEL_LPSS_ACPI
    PINCTRL PINCTRL_AMD PINCTRL_ALDERLAKE PINCTRL_TIGERLAKE PINCTRL_ICELAKE PINCTRL_CANNONLAKE PINCTRL_SUNRISEPOINT PINCTRL_BROXTON PINCTRL_GEMINILAKE PINCTRL_CHERRYVIEW PINCTRL_BAYTRAIL PINCTRL_METEORLAKE GPIOLIB BACKLIGHT_CLASS_DEVICE SUSPEND PM_SLEEP HIBERNATION
    CPU_FREQ CPU_FREQ_GOV_POWERSAVE CPU_FREQ_GOV_PERFORMANCE CPU_FREQ_GOV_ONDEMAND CPU_FREQ_GOV_CONSERVATIVE CPU_FREQ_GOV_SCHEDUTIL
    X86_INTEL_PSTATE X86_ACPI_CPUFREQ X86_AMD_PSTATE CPU_IDLE INTEL_IDLE PCIEASPM PCIEASPM_POWERSAVE
    PM RFKILL INPUT_LEDS)
  local modules=(DRM_I915 DRM_VIRTIO_GPU DRM_VMWGFX DRM_BOCHS DRM_CIRRUS_QEMU DRM_AST DRM_MGAG200 DRM_QXL
    DRM_NOUVEAU DRM_AMDGPU DRM_RADEON)
  # bool sub-options that widen amdgpu/radeon to older GPUs (enabled only if the module above exists)
  local gpu_opts=(DRM_AMDGPU_SI DRM_AMDGPU_CIK)
  local off=(SOUND KVM WLAN BT MEDIA_SUPPORT INFINIBAND NFC HAMRADIO CAN CMDLINE_OVERRIDE MODULE_SIG FB_SIMPLE)
  local crit_y=(EXT4_FS ISO9660_FS BLK_DEV_SR BLK_DEV_LOOP OVERLAY_FS VFAT_FS NTFS3_FS EXFAT_FS BLK_DEV_SD SATA_AHCI BLK_DEV_NVME USB_XHCI_HCD USB_EHCI_HCD USB_STORAGE EFI_STUB DEVTMPFS
    BINFMT_SCRIPT CMDLINE_BOOL MODULES FONT_TER16x32 FONT_10x18 DRM DRM_SIMPLEDRM SYSFB_SIMPLEFB DRM_FBDEV_EMULATION FRAMEBUFFER_CONSOLE UNIX98_PTYS)

  log "kernel: configuring ($(sed -n 's/^SUBLEVEL = //p' "$WORK/linux/Makefile") patch level)"
  M -s defconfig
  # keep the build small: only what is listed below becomes a module
  sed -i -E 's/^(CONFIG_[A-Za-z0-9_]+)=m$/# \1 is not set/' "$KO/.config"
  for o in "${builtin[@]}"; do C -e "$o"; done
  for o in "${modules[@]}"; do C -m "$o"; done
  for o in "${off[@]}"; do C -d "$o"; done
  # scripts/config upper-cases names, so the mixed-case font symbols are appended by hand
  printf 'CONFIG_FONT_8x16=y\nCONFIG_FONT_10x18=y\nCONFIG_FONT_TER16x32=y\n' >>"$KO/.config"
  C --set-str CMDLINE "$(cmdline "$LIVE_GUID")"
  for o in "${laptop[@]}"; do C -e "$o"; done
  C --set-val SATA_MOBILE_LPM_POLICY 3   # med_power_with_dipm for laptops (integer option)
  for o in "${gpu_opts[@]}"; do C -e "$o"; done   # amdgpu/radeon support for older GPU generations (SI/CIK)
  M -s olddefconfig
  for o in "${crit_y[@]}"; do grep -q "^CONFIG_$o=y" "$KO/.config" || die "kernel option CONFIG_$o is not built in (dependency problem?)"; done
  for o in "${laptop[@]}"; do grep -q "^CONFIG_$o=[ym]" "$KO/.config" || warn "laptop option CONFIG_$o is unavailable (skipped)"; done
  for o in "${modules[@]}"; do grep -q "^CONFIG_$o=m" "$KO/.config" || warn "kernel module CONFIG_$o is unavailable in this kernel series (skipped)"; done
  for o in "${gpu_opts[@]}"; do grep -q "^CONFIG_$o=y" "$KO/.config" || warn "GPU option CONFIG_$o is unavailable (older-GPU support skipped)"; done

  log "kernel: compiling (this is the slowest step, 20-40 minutes on 2 cores)"
  M -s -j"$JOBS" bzImage modules >"$LOGS/kernel.log" 2>&1 || { tail -n 30 "$LOGS/kernel.log" >&2; die "kernel build failed"; }
  local rel; rel=$(M -s kernelrelease)
  cp "$KO/arch/x86/boot/bzImage" "$OUTD/bzImage.live"
  rm -rf "$PKG/kmods"; mkdir -p "$PKG/kmods"
  M -s INSTALL_MOD_PATH="$PKG/kmods" INSTALL_MOD_STRIP=1 modules_install >>"$LOGS/kernel.log" 2>&1
  rm -f "$PKG/kmods/lib/modules/$rel/build" "$PKG/kmods/lib/modules/$rel/source"
  echo "$rel" >"$OUTD/kernel.release"

  log "kernel: second image for installed systems"
  C --set-str CMDLINE "$(cmdline "$INST_GUID")"
  M -s olddefconfig
  M -s -j"$JOBS" bzImage >>"$LOGS/kernel.log" 2>&1
  cp "$KO/arch/x86/boot/bzImage" "$OUTD/bzImage.inst"
  cp "$KO/.config" "$OUTD/kernel.config"
  mark kernel
  log "kernel $rel ready ($(du -sh "$PKG/kmods/lib/modules/$rel" | cut -f1) of modules)"
}
