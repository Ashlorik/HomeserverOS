@echo off
REM Specify the path to your QEMU installation
SET QEMU_PATH="C:\Program Files\qemu\qemu-system-x86_64.exe"

REM Auto-extract the v3-full image if the raw file is missing
IF NOT EXIST "homeserver-v3-full.img" (
    IF EXIST "homeserver-v3-full.img.xz" (
        echo [*] Unpacking homeserver-v3-full.img.xz... please wait...
        tar -xf homeserver-v3-full.img.xz
    ) ELSE (
        echo [!] Error: Neither homeserver-v3-full.img nor homeserver-v3-full.img.xz was found!
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

REM Run QEMU with UEFI, 1024MB RAM, and basic network backend
%QEMU_PATH% -m 1024 ^
            -bios OVMF.fd ^
            -drive file=homeserver-v3-full.img,format=raw ^
            -net nic,model=virtio ^
            -net user ^
            -rtc base=localtime

pause
