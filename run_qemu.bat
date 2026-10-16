@echo off
REM Run HomeserverOS in QEMU on Windows (UEFI via OVMF.fd).
REM Put the downloaded homeserver-v0.3a-lite.img.xz (or homeserver-v0.3a.img.xz) next to this file.
SET QEMU_PATH="C:\Program Files\qemu\qemu-system-x86_64.exe"
SET IMG=homeserver-v0.3a-lite.img

IF NOT EXIST "%IMG%" (
    IF EXIST "%IMG%.xz" (
        echo [*] Extracting %IMG%.xz ... please wait...
        tar -xf "%IMG%.xz"
    ) ELSE (
        echo [!] Neither %IMG% nor %IMG%.xz was found! ^(for the full image set IMG=homeserver-v0.3a.img^)
        pause
        exit /b
    )
)
IF NOT EXIST "OVMF.fd" (
    echo [!] OVMF.fd is missing!
    pause
    exit /b
)

%QEMU_PATH% -m 1536 -machine q35^
 -bios OVMF.fd^
 -device ahci,id=ahci^
 -drive file=%IMG%,format=raw,if=none,id=os_disk^
 -device ide-hd,bus=ahci.0,drive=os_disk^
 -nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22^
 -rtc base=localtime

REM SSH into the VM:  ssh -p 2222 root@localhost   (enable SSH in the setup wizard)
pause
