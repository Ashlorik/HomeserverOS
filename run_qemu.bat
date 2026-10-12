@echo off
REM Specify the path to your QEMU installation
SET QEMU_PATH="C:\Program Files\qemu\qemu-system-x86_64.exe"

REM Check if the raw image file exists
IF NOT EXIST "homeserver-tui.img" (
    echo [!] Error: homeserver-tui.img not found in the current folder!
    echo Please extract homeserver-tui.img.xz first!
    echo.
    pause
    exit /b
)

REM Check if UEFI bios exists
IF NOT EXIST "OVMF.fd" (
    echo [!] Error: OVMF.fd bios file is missing!
    echo Please download it and place it in this folder.
    echo.
    pause
    exit /b
)

REM Run QEMU with UEFI support, 1024MB RAM, and basic user networking
%QEMU_PATH% -m 1024 ^
            -bios OVMF.fd ^
            -drive file=homeserver-tui.img,format=raw ^
            -net nic,model=virtio ^
            -net user ^
            -rtc base=localtime

pause
