# HomeServer OS (text-mode edition) — pre-built image / готовый образ

## English

**File:** `homeserver-tui.img.xz` (about 14 MB; unpacks to a 500 MiB raw disk image). SHA-256 of the compressed file: see `SHA256SUMS`. SHA-256 of the unpacked image: `3469dc5834147d5d6a7fa117ebc75877666fa38c01bd61a1b87d914876a19875`.

**What you get:** GPT disk, partition 1 = 128 MiB FAT32 ESP, partition 2 = ext4 root. Linux 6.12.112 (no modules; SATA/AHCI, NVMe, USB storage, MMC/SD, common Intel/Realtek/Broadcom NICs built in) + static BusyBox 1.36.1 + `dialog`, `btop`, `nnn`. On first boot a setup wizard asks for server name, root password and network; after login a menu offers dashboard, files, network, storage, services, logs, settings and power. See `README.md` in `homeserver-os-v2.zip` for the full tour and screenshots. Kernel `.config` is included.

**Boot method:** no GRUB. The kernel is a UEFI program (`EFI/BOOT/BOOTX64.EFI` on the ESP) with its command line compiled in (root=PARTUUID=543b6784-7d0a-4a8b-9fb7-ab866057a8c3 …, dark console palette, quiet boot). Build it yourself with `build.sh` if you want GRUB.

**Not boot-tested on UEFI or real hardware** (the build sandbox has no QEMU/OVMF). Checked: GPT CRCs, `fsck.fat` and `e2fsck` clean, kernel is a valid x86-64 UEFI application and matches the bytes on the ESP; the wizard (DHCP and static paths), login, every menu screen, web/NTP/cron services, btop and nnn were run end to end in isolated terminals. Large console fonts and drivers on your hardware are unverified.

**Security:** root has no password until the wizard sets one (it runs on the physical console). A static web page is served on port 80 by default; turn it off in *Services*. There is no SSH. Secure Boot must be off.

**Write to disk** (double-check the device; this erases it):
```bash
xz -dc homeserver-tui.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
sudo sgdisk -e /dev/sdX     # optional on larger disks
```
balenaEtcher accepts `.img.xz` directly. In firmware setup enable UEFI and disable Secure Boot. Test in a VM first if you can: `qemu-system-x86_64 -m 1024 -bios OVMF.fd -drive file=homeserver-tui.img,format=raw`.

## Русский

**Файл:** `homeserver-tui.img.xz` (около 14 МБ; распаковывается в сырой образ диска 500 МиБ). SHA-256 архива — в `SHA256SUMS`. SHA-256 распакованного образа: `3469dc5834147d5d6a7fa117ebc75877666fa38c01bd61a1b87d914876a19875`.

**Что внутри:** диск GPT, раздел 1 — ESP FAT32 128 МиБ, раздел 2 — корень ext4. Linux 6.12.112 (без модулей; встроены SATA/AHCI, NVMe, USB-накопители, MMC/SD, популярные сетевые карты Intel/Realtek/Broadcom) + статический BusyBox 1.36.1 + `dialog`, `btop`, `nnn`. При первой загрузке мастер спрашивает имя сервера, пароль root и сеть; после входа меню даёт панель, файлы, сеть, диски, службы, журналы, настройки и питание. Полный обзор и скриншоты — в `README.md` внутри `homeserver-os-v2.zip`. Файл `.config` ядра приложен.

**Способ загрузки:** без GRUB. Ядро — это UEFI-программа (`EFI/BOOT/BOOTX64.EFI` на ESP) со вшитой командной строкой (root=PARTUUID=543b6784-7d0a-4a8b-9fb7-ab866057a8c3 …, тёмная палитра консоли, тихая загрузка). Если нужен GRUB — соберите образ сами через `build.sh`.

**Загрузка по UEFI и на реальном железе не проверялась** (в среде сборки нет QEMU/OVMF). Проверено: CRC таблицы GPT, `fsck.fat` и `e2fsck` без ошибок, ядро — корректное UEFI-приложение x86-64, совпадающее с байтами на ESP; мастер (DHCP и статический адрес), вход, все экраны меню, службы web/NTP/cron, btop и nnn прогнаны целиком в изолированных терминалах. Крупные шрифты консоли и драйверы под ваше железо не проверены.

**Безопасность:** пока мастер не задаст пароль, у root его нет (мастер работает на физической консоли). По умолчанию на порту 80 отдаётся статическая страница; отключите её в разделе *Services*. SSH нет. Secure Boot должен быть выключен.

**Запись на диск** (проверьте имя устройства — данные будут стёрты):
```bash
xz -dc homeserver-tui.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
sudo sgdisk -e /dev/sdX     # по желанию, на дисках большего размера
```
balenaEtcher принимает `.img.xz` напрямую. В настройках прошивки включите UEFI и отключите Secure Boot. По возможности сначала проверьте в ВМ: `qemu-system-x86_64 -m 1024 -bios OVMF.fd -drive file=homeserver-tui.img,format=raw`.
