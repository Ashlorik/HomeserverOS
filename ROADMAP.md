# ROADMAP — Console 1.0

Решение: сначала доводим **консольную систему** до состояния «ставится на любой ПК/ноутбук и пригодна как домашний сервер», потом 3D и графическая оболочка.
Decision: finish the console system first; 3D and a graphical shell come after.

**Критерий Console 1.0:** один образ/ISO ставится и запускается на UEFI и на старом BIOS, видит железо ноутбука, экономит энергию, доступен по SSH, умеет обновляться и запускать сторонние программы; загрузка и установка проверяются в CI (QEMU).

## Этап A — основа (сделано в этой ветке)
- [x] CI: сборка + загрузка полного и lite образов в QEMU/OVMF (зелёный).
- [x] Пользователь `admin` (повседневный) + `root` (администрирование через меню, `su`); вход root по SSH выключен по умолчанию.
- [x] `hs-get` + каталог (fastfetch, micro, ripgrep): HTTPS, проверка sha256, установка в `/opt/apps`; совместимость с glibc-программами (компат-слой + `ld.so.cache`).
- [x] Экран запуска с информацией: CPU, память, диски, сеть, батарея, режим загрузки (UEFI/BIOS), live/установленная, время запуска (`hs-sysinfo`).

## Этап B — загрузка «на любой системе» (Boot 2.0)
- [ ] Гибридный **ISO** (BIOS + UEFI) через `grub-mkrescue`: меню «Live / Установка / Загрузить систему с диска».
- [ ] Live-система целиком в **RAM** (initramfs): флешку можно вынуть, всё как с ISO; убирает привязку к PARTUUID в ядре и ограничение «одна установка на ПК».
- [ ] Установка на старый BIOS: GRUB i386-pc + раздел bios_grub; UEFI-путь остаётся.
- [ ] CI: загрузка ISO в QEMU и в режиме BIOS, и в режиме UEFI; тест «установка → перезагрузка с диска».
- [ ] `hs-install --disk DEV --yes` без диалогов (для тестов).

## Этап C — железо, ноутбук, энергия (одним изменением конфигурации ядра)
- [ ] Ноутбук: батарея/AC/крышка/кнопки (ACPI), подсветка экрана, сенсорная панель (I2C-HID, multitouch), сон (s2idle), `acpid`.
- [ ] **Энергосбережение** (`hs-power`): профили «Максимум экономии / Баланс / Производительность»: governor `powersave`, максимальная частота = минимальной аппаратной (**0.4 ГГц, если процессор это позволяет**; у многих Intel/AMD минимум 0.4–0.8 ГГц), отключение turbo, runtime PM для PCI/USB, link power management SATA, экономия Wi-Fi, гашение экрана.
- [ ] **Драйверы:** широкий набор модулей (Ethernet, Wi-Fi, USB, NVMe, звук, графика), подгрузка по modalias при старте, выборочный набор прошивок (linux-firmware: Intel/Realtek/Atheros/MediaTek Wi-Fi, i915). Образ вырастет.
- [ ] Wi-Fi: wpa_supplicant + выбор сети в TUI.

## Этап D — TUI 2.0
- [ ] Мышь/тачпад в консоли: ядро (`mousedev`, PS/2, HID) + `gpm`, ncurses с поддержкой gpm; проверить `dialog`/`nnn`/`btop`. Ограничение: консоль Linux не передаёт мышь как терминал, поэтому для «настоящей» мыши во всех экранах потребуется собственный TUI-интерфейс (ncurses + gpm).
- [ ] Новый единый интерфейс: боковое меню, живая статистика, подсказки, цвета, управление мышью.
- [ ] Русский язык: раскладка консоли, кириллический шрифт, перевод меню, выбор языка в мастере.

## Этап E — сервер
- [ ] Безопасность: ключи SSH как основной вход, файрвол (профили), задержка при подборе пароля.
- [ ] Хранилище: мастер второго диска, SMART, сетевые папки (SMB/FTP), резервная копия `/etc/hs`.
- [ ] `hs-update`: обновление из GitHub Releases с проверкой sha256 и откатом.
- [ ] Страница статуса по HTTP, ротация логов, mDNS.

## После Console 1.0
1. 3D без оконной системы: kmscube / glmark2 через DRM/KMS.
2. Графическая оболочка (Wayland: labwc или sway + foot).
3. Свой 3D-движок поверх DRM/GBM/GLES; FurMark — только после п.2 (закрытый, нужен X11/Wayland).

## Порядок
A (готово) → B (ISO/BIOS/RAM — база для всех проверок) → C (одной сборкой ядра) → D → E. Каждый шаг заканчивается проверкой в CI или тестовом окружении; то, что нельзя проверить без железа (батарея, частота, тачпад), помечается как «не проверено на железе».

## Progress (release 1)
- [x] hs-get, admin user, fastfetch, boot summary
- [x] hs-power: profiles ultra/saver/balanced/performance/auto (CPU min freq, EPP, turbo, ASPM, SATA/USB/PCI PM, backlight) - tested on a fake sysfs only
- [x] laptop kernel options (battery, AC, lid/buttons, touchpad, I2C-HID, cpufreq, backlight) - checked softly at build
- [ ] acpid: power button / lid actions
- [ ] release 2: TUI v2 (gpm mouse/touchpad), hs-firmware driver manager
- [ ] release 3: Boot 2.0 (initramfs, GRUB BIOS+UEFI, ISO, live in RAM)
