@echo off
REM Specify the path to your QEMU installation
SET QEMU_PATH="C:\Program Files\qemu\qemu-system-x86_64.exe"

REM Auto-extract the image if the raw file is missing
IF NOT EXIST "homeserver-tui.img" (
    IF EXIST "homeserver-tui.img.xz" (
        echo [*] Extracting homeserver-tui.img.xz... please wait...
        tar -xf homeserver-tui.img.xz
    ) ELSE (
        echo [!] Error: Neither homeserver-tui.img nor homeserver-tui.img.xz was found!
        pause
        exit /b
    )
)

REM Check if UEFI bios exists
IF NOT EXIST "OVMF.fd" (
    echo [!] Error: OVMF.fd bios file is missing!
    pause
    exit /b
)

REM Run QEMU with full drive emulation and fixed network backends
%QEMU_PATH% -m 1024^
 -bios OVMF.fd^
 -device ahci,id=ahci^
 -drive file=homeserver-tui.img,format=raw,if=none,id=os_disk^
 -device ide-hd,bus=ahci.0,drive=os_disk^
 -net nic,model=virtio-net-pci^
 -net user^
 -rtc base=localtime

pause
