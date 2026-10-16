#!/usr/bin/env bash
# End-to-end installer test: the live image installs itself onto a blank virtual disk (hs-install --disk vda --yes, started by the
# kernel parameter hs.autoinstall=vda), then the installed system alone is booted once with UEFI and once with legacy BIOS.
set -u
sudo apt-get install -y qemu-system-x86 >/dev/null
OVMF=$PWD/OVMF.fd; KERNEL=build/out/bzImage.live; IMG=out/homeserver-v0.3a-lite.img
[ -f "$KERNEL" ] || { echo "missing $KERNEL"; exit 1; }
rm -f target.img serial.log; truncate -s 4G target.img

timeout 900 qemu-system-x86_64 -m 1536 -machine q35 -kernel "$KERNEL" -append "hs.autoinstall=vda" \
  -drive file="$IMG",format=raw,if=none,id=d,snapshot=on -device ahci,id=ahci -device ide-hd,drive=d,bus=ahci.0 \
  -drive file=target.img,format=raw,if=none,id=t -device virtio-blk-pci,drive=t \
  -display none -serial file:serial.log -no-reboot &
q=$!; ok=0
for i in $(seq 1 420); do
  grep -q HS-INSTALL-OK serial.log 2>/dev/null && { ok=1; break; }
  grep -q HS-INSTALL-FAILED serial.log 2>/dev/null && break
  sleep 2
done
for i in $(seq 1 30); do kill -0 $q 2>/dev/null || break; sleep 1; done   # the live system powers itself off after installing
kill $q 2>/dev/null || true; wait $q 2>/dev/null || true
echo "=== installer serial log"; tail -n 30 serial.log 2>/dev/null || echo "(no serial output)"
[ $ok = 1 ] || { echo "installation did not finish"; exit 1; }

for mode in uefi bios; do
  fw=(); [ $mode = uefi ] && fw=(-bios "$OVMF")
  rm -f serial.log
  timeout 150 qemu-system-x86_64 -m 1024 -machine q35 "${fw[@]}" \
    -drive file=target.img,format=raw,if=none,id=d,snapshot=on -device ahci,id=ahci -device ide-hd,drive=d,bus=ahci.0 \
    -nic user,model=virtio-net-pci -display none -serial file:serial.log -no-reboot &
  q=$!; ok=0
  for i in $(seq 1 70); do grep -q HOMESERVER-OS-BOOT-OK serial.log 2>/dev/null && { ok=1; break; }; sleep 2; done
  kill $q 2>/dev/null || true; wait $q 2>/dev/null || true
  echo "=== installed system, $mode boot"; tail -n 25 serial.log 2>/dev/null || echo "(no serial output)"
  [ $ok = 1 ] || { echo "the installed system did not boot in $mode mode"; exit 1; }
done
echo "installer test passed"
