@echo off
REM HomeServer OS launcher for Windows: menu with image choice/download, UEFI or BIOS boot, memory, installer test.
REM Needs QEMU (the launcher offers to install it) and 7-Zip for .xz images (also offered). Settings are saved in hs-launcher.json.
REM   run_qemu.bat          open the menu
REM   run_qemu.bat -Start   start immediately with the saved settings
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0hs-launcher.ps1" %*
IF ERRORLEVEL 1 pause
