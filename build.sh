#!/bin/env bash
set -euo pipefail

OUTDIR="$(pwd)/out"
IMG="${OUTDIR}/homeserver-tui.img"
SIZE_MB=500
WORK="$(pwd)/work"
ROOT_FS="${WORK}/rootfs"

# Сброс и создание чистых папок
rm -rf "${OUTDIR}" "${WORK}"
mkdir -p "${OUTDIR}" "${WORK}" "${ROOT_FS}"

echo "[1/4] Создание структуры каталогов файловой системы..."
mkdir -p "${ROOT_FS}"/{dev,proc,sys,etc,bin,sbin,usr/bin,usr/sbin,root,mnt,tmp,var}

# Переносим ваш overlay, если он существует
if [ -d "overlay" ]; then
    echo "[*] Копирование кастомного overlay..."
    cp -r overlay/* "${ROOT_FS}/"
fi

# Простейший скрипт инициализации
cat << 'EOF' > "${ROOT_FS}/init"
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev
clear
echo "============================================="
echo " Welcome to HomeserverOS (Cloud-Built)       "
echo "============================================="
exec /bin/sh
EOF
chmod +x "${ROOT_FS}/init"

echo "[2/4] Компиляция BusyBox (Имитация сборщика)..."
# В реальном окружении здесь распаковывается и собирается Busybox в ${ROOT_FS}
# Для демонстрации используем локальный статический busybox хоста, если он доступен
cp /bin/busybox "${ROOT_FS}/bin/" 2>/dev/null || wget -qO "${ROOT_FS}/bin/busybox" https://busybox.net
chmod +x "${ROOT_FS}/bin/busybox"
"${ROOT_FS}/bin/busybox" --install -s "${ROOT_FS}/bin"

echo "[3/4] Сборка образа диска без loop-устройств (mtools)..."
# Создаем пустой файл образа
dd if=/dev/zero of="${IMG}" bs=1M count="${SIZE_MB}" status=none

# Создаем GPT разметку утилитой sgdisk (не требует root)
sgdisk -o "${IMG}"
sgdisk -n 1:2048:409600 -t 1:ef00 -c 1:"EFI" "${IMG}"
sgdisk -n 2:409601:0 -t 2:8300 -c 2:"Root" "${IMG}"

# Подготавливаем EFI раздел в файле через mtools (без монтирования)
dd if=/dev/zero of="${WORK}/efi.part" bs=1M count=199 status=none
mkfs.vfat -F 32 "${WORK}/efi.part"
mmd -i "${WORK}/efi.part" ::/EFI
mmd -i "${WORK}/efi.part" ::/EFI/BOOT

# Если в корне есть OVMF.fd или ядро, закидываем их в EFI образ
if [ -f "OVMF.fd" ]; then
    mcopy -i "${WORK}/efi.part" OVMF.fd ::/EFI/BOOT/BOOTX64.EFI || true
fi

echo "[4/4] Сборка корневого EXT4 раздела (genext2fs)..."
# Собираем EXT4 образ напрямую из папки с помощью утилиты из пространства пользователя
sudo apt-get install -y genext2fs || true
genext2fs -d "${ROOT_FS}" -b 300000 -B 1024 "${WORK}/root.part"
# Конвертируем в полноценный ext4
tune2fs -O extents,uninit_bg,dir_index "${WORK}/root.part" || true

# Прожигаем готовые разделы внутрь итогового образа по правильным смещениям (dd)
dd if="${WORK}/efi.part" of="${IMG}" bs=512 seek=2048 conv=notrunc status=none
dd if="${WORK}/root.part" of="${IMG}" bs=512 seek=409601 conv=notrunc status=none

echo "==> Сборка успешно завершена! Образ: ${IMG}"
