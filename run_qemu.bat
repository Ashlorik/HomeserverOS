@echo off
REM Specify the path to your QEMU installation
SET QEMU_PATH="C:\Program Files\qemu\qemu-system-x86_64.exe"

REM Auto-extract the v3-lite image if the raw file is missing
IF NOT EXIST "homeserver-v3-lite.img" (
    IF EXIST "homeserver-v3-lite.img.xz" (
        echo [*] Unpacking homeserver-v3-lite.img.xz... please wait...
        tar -xf homeserver-v3-lite.img.xz
    ) ELSE (
        echo [!] Error: Neither homeserver-v3-lite.img nor homeserver-v3-lite.img.xz was found!
        pause
        exit /b
    )
)

REM Check if UEFI bios exists
IF NOT EXIST "OVMF.fd" (
    echo [!] Error: OVMF.fd bios file is missing!
    echo Please copy OVMF.fd from your v2 folder into this directory.
    echo.
    pause
    exit /b
)

REM Run QEMU with UEFI, 1024MB RAM, and basic network backend
%QEMU_PATH% -m 1024 ^
            -bios OVMF.fd ^
            -drive file=homeserver-v3-lite.img,format=raw ^
            -net nic,model=virtio ^
            -net user ^
            -rtc base=localtime

pause
