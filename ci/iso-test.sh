#!/usr/bin/env bash
# Boot the ISO as a CD-ROM in QEMU with UEFI (OVMF) and legacy BIOS; wait for the serial boot marker.
set -u
sudo apt-get install -y qemu-system-x86 >/dev/null
OVMF=$PWD/OVMF.fd
for mode in uefi bios; do
  fw=(); [ $mode = uefi ] && fw=(-bios "$OVMF")
  rm -f serial.log
  timeout 200 qemu-system-x86_64 -m 256 -smp 1 -machine q35 "${fw[@]}" -cdrom out/homeserver.iso -boot d \
    -nic user,model=virtio-net-pci -display none -serial file:serial.log -no-reboot &
  q=$!; ok=0
  for i in $(seq 1 90); do grep -q HOMESERVER-OS-BOOT-OK serial.log 2>/dev/null && { ok=1; break; }; sleep 2; done
  kill $q 2>/dev/null || true; wait $q 2>/dev/null || true
  echo "=== serial log (ISO, $mode)"; tail -n 40 serial.log 2>/dev/null || echo "(no serial output)"
  [ $ok = 1 ] || { echo "ISO did not boot in $mode mode"; exit 1; }
done
