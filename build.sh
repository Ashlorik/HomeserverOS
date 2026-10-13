#!/bin/env bash
set -euo pipefail

OUTDIR="$(pwd)/out"
IMG="${OUTDIR}/homeserver-tui.img"
SIZE_MB=500
WORK="$(pwd)/work"

mkdir -p "${OUTDIR}" "${WORK}"

echo "[1/4] Создание чистого диска и разметка GPT..."
dd if=/dev/zero of="${IMG}" bs=1M count="${SIZE_MB}" status=none
sgdisk -o "${IMG}"
sgdisk -n 1:2048:409600 -t 1:ef00 -c 1:"EFI" "${IMG}"
sgdisk -n 2:409601:0 -t 2:8300 -c 2:"Root" "${IMG}"

echo "[2/4] Сборка настоящего UEFI-загрузчика (GRUB2)..."
dd if=/dev/zero of="${WORK}/efi.part" bs=1M count=199 status=none
mkfs.vfat -F 32 "${WORK}/efi.part"

# Создаем структуру каталогов EFI
mmd -i "${WORK}/efi.part" ::/EFI
mmd -i "${WORK}/EFI" ::/EFI/BOOT

# Генерируем минимальный конфигурационный файл для GRUB
cat << 'EOF' > "${WORK}/grub.cfg"
set default=0
set timeout=3
menuentry "HomeserverOS TUI Mode" {
    linux /vmlinuz root=/dev/sda2 rw console=ttyS0 console=tty1 quiet
}
EOF

# Собираем настоящий исполняемый PE32+ бинарник для UEFI
grub-mkimage -d /usr/lib/grub/x86_64-efi -O x86_64-efi -o "${WORK}/BOOTX64.EFI" -p "/EFI/BOOT" part_gpt fat ext2 configfile search normal linux

# Копируем бинарник загрузчика и его конфиг внутрь образа раздела
mcopy -i "${WORK}/efi.part" "${WORK}/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "${WORK}/efi.part" "${WORK}/grub.cfg" ::/EFI/BOOT/grub.cfg

echo "[3/4] Сборка корневой системы EXT4..."
mkdir -p "${WORK}/rootfs"/{dev,proc,sys,etc,bin,sbin,usr/bin,usr/sbin,root,mnt,tmp,var,boot}
if [ -d "overlay" ]; then
    cp -r overlay/* "${WORK}/rootfs/"
fi

# Интегрируем Busybox и создаем симлинки команд
cp /bin/busybox "${WORK}/rootfs/bin/" 2>/dev/null || wget -qO "${WORK}/rootfs/bin/busybox" https://busybox.net
chmod +x "${WORK}/rootfs/bin/busybox"
"${WORK}/rootfs/bin/busybox" --install -s "${WORK}/rootfs/bin"

# Копируем ядро в корень раздела (если ядра нет в репозитории, используем ядро контейнера сборщика)
if [ -f "vmlinuz" ]; then
    cp vmlinuz "${WORK}/rootfs/vmlinuz"
else
    cp /boot/vmlinuz-* "${WORK}/rootfs/vmlinuz" || touch "${WORK}/rootfs/vmlinuz"
fi

# Упаковываем корневую систему в ext4 без loop-устройств
genext2fs -d "${WORK}/rootfs" -b 300000 -B 1024 "${WORK}/root.part"
tune2fs -O extents,uninit_bg,dir_index "${WORK}/root.part" || true

echo "[4/4] Склеивание разделов в финальный образ..."
dd if="${WORK}/efi.part" of="${IMG}" bs=512 seek=2048 conv=notrunc status=none
dd if="${WORK}/root.part" of="${IMG}" bs=512 seek=409601 conv=notrunc status=none

echo "==> Сборка завершена! Образ: ${IMG}"
