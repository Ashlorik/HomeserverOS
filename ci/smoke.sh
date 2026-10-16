#!/usr/bin/env bash
# Boot the images in QEMU, once with UEFI (OVMF) and once with legacy BIOS (SeaBIOS), and wait for the serial boot marker.
set -u
sudo apt-get install -y qemu-system-x86 >/dev/null
OVMF=$PWD/OVMF.fd   # UEFI firmware shipped in this repository
for img in homeserver-v0.3a-lite homeserver-v0.3a; do
  for mode in uefi bios; do
    fw=(); [ $mode = uefi ] && fw=(-bios "$OVMF")
    rm -f serial.log
    timeout 150 qemu-system-x86_64 -m 1024 -machine q35 "${fw[@]}" \
      -drive file=out/$img.img,format=raw,if=none,id=d,snapshot=on -device ahci,id=ahci -device ide-hd,drive=d,bus=ahci.0 \
      -nic user,model=virtio-net-pci -display none -serial file:serial.log -no-reboot &
    q=$!; ok=0
    for i in $(seq 1 70); do grep -q HOMESERVER-OS-BOOT-OK serial.log 2>/dev/null && { ok=1; break; }; sleep 2; done
    kill $q 2>/dev/null || true; wait $q 2>/dev/null || true
    echo "=== serial log ($img, $mode)"; tail -n 40 serial.log 2>/dev/null || echo "(no serial output)"
    [ $ok = 1 ] || { echo "$img did not boot to the system in $mode mode"; exit 1; }
  done
done
