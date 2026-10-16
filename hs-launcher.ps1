# HomeServer OS launcher for Windows (QEMU). Started by run_qemu.bat.
#   - picks an image (local files, or downloads the latest release from GitHub and checks its SHA-256)
#   - boot mode: UEFI or legacy BIOS; memory, CPUs, acceleration, SSH port, console/window
#   - safe mode (disk changes are discarded on exit) or persistent
#   - installer test: attaches an empty virtual disk, then boots the installed system from it alone
# Needs Windows 10/11 and QEMU (winget install -e --id SoftwareFreedomConservancy.QEMU). 7-Zip is used to unpack .xz.
param([switch]$Start)
$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
$Repo = 'Ashlorik/HomeserverOS'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Here
$CfgFile = Join-Path $Here 'hs-launcher.json'
$TargetDisk = Join-Path $Here 'hs-target.qcow2'

$Cfg = [ordered]@{ Image = ''; Boot = 'uefi'; Ram = 1536; Cpus = 2; Accel = 'auto'; Safe = $true; Ssh = 2222; Console = $false; TestInstall = $false; BootTarget = $false }
if (Test-Path $CfgFile) {
  try { $old = Get-Content $CfgFile -Raw | ConvertFrom-Json; foreach ($k in @($Cfg.Keys)) { if ($null -ne $old.$k) { $Cfg[$k] = $old.$k } } } catch {}
}
function Save-Cfg { try { ($Cfg | ConvertTo-Json) | Set-Content -Encoding UTF8 $CfgFile } catch {} }

function Find-Tool([string]$name, [string[]]$extra) {
  $c = Get-Command $name -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  foreach ($p in $extra) { if (Test-Path $p) { return $p } }
  return $null
}
function Find-Qemu { Find-Tool 'qemu-system-x86_64.exe' @("$env:ProgramFiles\qemu\qemu-system-x86_64.exe", "${env:ProgramFiles(x86)}\qemu\qemu-system-x86_64.exe", "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe") }
function Find-7z { Find-Tool '7z.exe' @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") }
function Pause-Key { Read-Host 'Press Enter' | Out-Null }

function Ensure-Winget([string]$id, [string]$what) {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Write-Host "Install $what manually (winget is not available)."; return $false }
  $a = Read-Host "$what is missing. Install it now with winget? (y/n)"
  if ($a -ne 'y') { return $false }
  winget install -e --id $id --accept-package-agreements --accept-source-agreements
  return $true
}

function Get-LocalImages {
  Get-ChildItem -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '\.img(\.xz)?$' -and $_.Name -notlike 'hs-target*' } | Sort-Object Name
}

function Expand-Image([string]$xz) {
  $out = $xz -replace '\.xz$', ''
  if (Test-Path $out) { return $out }
  Write-Host "Unpacking $xz (this takes a minute) ..."
  $z = Find-7z
  if (-not $z) {
    if (Ensure-Winget '7zip.7zip' '7-Zip') { $z = Find-7z }
  }
  if ($z) { & $z x -y "-o$Here" $xz | Out-Null }
  else { try { tar -xf $xz 2>$null } catch {} }
  if (-not (Test-Path $out)) { throw "Could not unpack $xz. Install 7-Zip (https://7-zip.org) and try again." }
  return $out
}

function Choose-Image {
  $imgs = @(Get-LocalImages)
  if ($imgs.Count -eq 0) { Write-Host 'No .img / .img.xz files in this folder. Use "Download" first.'; Pause-Key; return }
  Write-Host ''
  for ($i = 0; $i -lt $imgs.Count; $i++) { Write-Host ("  {0}) {1}  ({2:N0} MB)" -f ($i + 1), $imgs[$i].Name, ($imgs[$i].Length / 1MB)) }
  $n = Read-Host 'Image number (Enter = cancel)'
  if ($n -match '^\d+$' -and [int]$n -ge 1 -and [int]$n -le $imgs.Count) {
    $f = $imgs[[int]$n - 1].Name
    if ($f -like '*.xz') { $f = Expand-Image $f }
    $Cfg.Image = $f; Save-Cfg
  }
}

function Download-Image {
  Write-Host 'Asking GitHub for releases ...'
  $rels = @(Invoke-RestMethod "https://api.github.com/repos/$Repo/releases" -Headers @{ 'User-Agent' = 'hs-launcher' })
  if ($rels.Count -eq 0) { Write-Host 'No releases found.'; Pause-Key; return }
  $rels = @($rels | Select-Object -First 6)
  for ($i = 0; $i -lt $rels.Count; $i++) {
    $r = $rels[$i]; $tag = ''; if ($r.prerelease) { $tag = ' [pre-release]' }
    Write-Host ("  {0}) {1}  ({2}){3}  {4}" -f ($i + 1), $r.name, $r.tag_name, $tag, $r.published_at.ToString().Substring(0, 10))
  }
  $n = Read-Host 'Release number (Enter = 1, newest)'; if ($n -notmatch '^\d+$' -or [int]$n -lt 1 -or [int]$n -gt $rels.Count) { $n = 1 }
  $rel = $rels[[int]$n - 1]
  $assets = @($rel.assets | Where-Object { $_.name -like '*.img.xz' })
  if ($assets.Count -eq 0) { Write-Host 'This release has no images (the build may still be running).'; Pause-Key; return }
  for ($i = 0; $i -lt $assets.Count; $i++) { Write-Host ("  {0}) {1}  ({2:N0} MB)" -f ($i + 1), $assets[$i].name, ($assets[$i].size / 1MB)) }
  $m = Read-Host 'Image to download (Enter = 1)'; if ($m -notmatch '^\d+$' -or [int]$m -lt 1 -or [int]$m -gt $assets.Count) { $m = 1 }
  $a = $assets[[int]$m - 1]
  $dest = Join-Path $Here $a.name
  Write-Host "Downloading $($a.name) ..."
  $curl = Find-Tool 'curl.exe' @()
  if ($curl) { & $curl -L --fail -o $dest $a.browser_download_url; if ($LASTEXITCODE -ne 0) { throw 'download failed' } }
  else { $ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest $a.browser_download_url -OutFile $dest -UseBasicParsing }
  $sums = $rel.assets | Where-Object { $_.name -eq 'SHA256SUMS' } | Select-Object -First 1
  if ($sums) {
    $txt = (Invoke-WebRequest $sums.browser_download_url -UseBasicParsing).Content
    if ($txt -is [byte[]]) { $txt = [Text.Encoding]::ASCII.GetString($txt) }
    $h = (Get-FileHash -Algorithm SHA256 $dest).Hash.ToLower()
    if ($txt.ToLower().Contains($h)) { Write-Host "SHA-256 OK: $h" -ForegroundColor Green }
    else { Write-Host "SHA-256 DOES NOT MATCH the published checksums. Delete $($a.name) and download again." -ForegroundColor Red; Pause-Key; return }
  } else { Write-Host 'No SHA256SUMS in this release: the file was not verified.' -ForegroundColor Yellow }
  $img = Expand-Image $dest
  $Cfg.Image = $img; Save-Cfg
  Write-Host "Selected: $img" -ForegroundColor Green; Pause-Key
}

function Ensure-Ovmf {
  $p = Join-Path $Here 'OVMF.fd'
  if (Test-Path $p) { return $p }
  Write-Host 'OVMF.fd (UEFI firmware) is missing, downloading it from the repository ...'
  try { Invoke-WebRequest "https://github.com/$Repo/raw/main/OVMF.fd" -OutFile $p -UseBasicParsing } catch { Write-Host 'Download failed. Put OVMF.fd next to this script.' -ForegroundColor Red; return $null }
  return $p
}

function Start-Vm([string]$qemu) {
  $boot = $Cfg.Boot
  $qa = @('-m', "$($Cfg.Ram)", '-smp', "$($Cfg.Cpus)", '-machine', 'q35')
  if ($Cfg.Accel -eq 'auto') { $qa += @('-accel', 'whpx,kernel-irqchip=off', '-accel', 'tcg') } else { $qa += @('-accel', 'tcg') }
  if ($boot -eq 'uefi') { $o = Ensure-Ovmf; if (-not $o) { Pause-Key; return }; $qa += @('-bios', $o) }
  $qa += @('-device', 'ahci,id=ahci')
  $targetOnly = ($Cfg.TestInstall -and $Cfg.BootTarget)
  if (-not $targetOnly) {
    if (-not $Cfg.Image -or -not (Test-Path $Cfg.Image)) { Write-Host 'Choose or download an image first.' -ForegroundColor Yellow; Pause-Key; return }
    $snap = ''; if ($Cfg.Safe) { $snap = ',snapshot=on' }
    $qa += @('-drive', "file=$($Cfg.Image),format=raw,if=none,id=os$snap", '-device', 'ide-hd,bus=ahci.0,drive=os')
  }
  if ($Cfg.TestInstall) {
    if (-not (Test-Path $TargetDisk)) {
      $qi = Join-Path (Split-Path $qemu) 'qemu-img.exe'
      & $qi create -f qcow2 $TargetDisk 16G | Out-Null
      Write-Host 'Created an empty 16 GB virtual disk hs-target.qcow2 for the installer test.'
    }
    $qa += @('-drive', "file=$TargetDisk,format=qcow2,if=none,id=tgt", '-device', 'ide-hd,bus=ahci.1,drive=tgt')
    if ($targetOnly) { $qa += @('-boot', 'c') }
  }
  $qa += @('-nic', "user,model=virtio-net-pci,hostfwd=tcp::$($Cfg.Ssh)-:22", '-rtc', 'base=localtime')
  if ($Cfg.Console) { $qa += @('-display', 'none', '-serial', 'mon:stdio') }
  Write-Host ''
  Write-Host "Starting: boot=$boot, RAM=$($Cfg.Ram) MB, CPUs=$($Cfg.Cpus), safe mode=$($Cfg.Safe)" -ForegroundColor Cyan
  Write-Host "SSH (enable it in the system menu first):  ssh -p $($Cfg.Ssh) admin@localhost"
  if ($Cfg.Console) { Write-Host 'Console mode: the system output is shown here. Ctrl+A then X quits QEMU.' }
  & $qemu @args
  $Cfg.BootTarget = $false
}

$qemu = Find-Qemu
if (-not $qemu) {
  Write-Host 'QEMU was not found.' -ForegroundColor Yellow
  if (Ensure-Winget 'SoftwareFreedomConservancy.QEMU' 'QEMU') { $qemu = Find-Qemu }
  if (-not $qemu) { Write-Host 'Install QEMU from https://qemu.weilnetz.de/w64/ and run this again.'; Pause-Key; exit 1 }
}
$Cfg.BootTarget = $false
if ($Start) { Start-Vm $qemu; exit 0 }

while ($true) {
  Clear-Host
  $img = $Cfg.Image; if (-not $img) { $img = '(not chosen)' }
  $mode = 'UEFI'; if ($Cfg.Boot -eq 'bios') { $mode = 'legacy BIOS' }
  $safe = 'safe: changes are discarded when you close QEMU'; if (-not $Cfg.Safe) { $safe = 'persistent: changes are written to the image' }
  Write-Host '=== HomeServer OS - QEMU launcher ===' -ForegroundColor Cyan
  Write-Host "  Image:    $img"
  Write-Host "  Boot:     $mode     Memory: $($Cfg.Ram) MB     CPUs: $($Cfg.Cpus)     Accel: $($Cfg.Accel)"
  Write-Host "  Disk:     $safe"
  Write-Host "  Installer test disk: $($Cfg.TestInstall)     SSH port: $($Cfg.Ssh)     Console mode: $($Cfg.Console)"
  Write-Host ''
  Write-Host '  1) Start'
  Write-Host '  2) Choose image from this folder'
  Write-Host '  3) Download image from GitHub (releases, with SHA-256 check)'
  Write-Host '  4) Boot mode: UEFI / legacy BIOS'
  Write-Host '  5) Memory and CPUs'
  Write-Host '  6) Safe mode / persistent disk'
  Write-Host '  7) Installer test disk: on/off  (empty 16 GB disk to install onto)'
  Write-Host '  8) Boot the installed system from the test disk'
  Write-Host '  9) Console mode (serial output in this window) / graphical window'
  Write-Host '  A) Acceleration: auto (WHPX, falls back to software) / software only'
  Write-Host '  P) SSH port'
  Write-Host '  D) Delete the test disk'
  Write-Host '  Q) Quit'
  $c = (Read-Host 'Choice').ToUpper()
  try {
    switch ($c) {
      '1' { Start-Vm $qemu; Pause-Key }
      '2' { Choose-Image }
      '3' { Download-Image }
      '4' { if ($Cfg.Boot -eq 'uefi') { $Cfg.Boot = 'bios' } else { $Cfg.Boot = 'uefi' }; Save-Cfg }
      '5' { $r = Read-Host "Memory in MB [$($Cfg.Ram)]"; if ($r -match '^\d+$' -and [int]$r -ge 512) { $Cfg.Ram = [int]$r }
            $r = Read-Host "CPUs [$($Cfg.Cpus)]"; if ($r -match '^\d+$' -and [int]$r -ge 1 -and [int]$r -le 16) { $Cfg.Cpus = [int]$r }; Save-Cfg }
      '6' { $Cfg.Safe = -not $Cfg.Safe; Save-Cfg }
      '7' { $Cfg.TestInstall = -not $Cfg.TestInstall; Save-Cfg }
      '8' { if (-not (Test-Path $TargetDisk)) { Write-Host 'The test disk does not exist yet: turn it on (7), boot the live image and run the installer first.'; Pause-Key }
            else { $Cfg.TestInstall = $true; $Cfg.BootTarget = $true; Start-Vm $qemu; Pause-Key } }
      '9' { $Cfg.Console = -not $Cfg.Console; Save-Cfg }
      'A' { if ($Cfg.Accel -eq 'auto') { $Cfg.Accel = 'software' } else { $Cfg.Accel = 'auto' }; Save-Cfg }
      'P' { $r = Read-Host "SSH port on this PC [$($Cfg.Ssh)]"; if ($r -match '^\d+$' -and [int]$r -ge 1024 -and [int]$r -le 65535) { $Cfg.Ssh = [int]$r }; Save-Cfg }
      'D' { if (Test-Path $TargetDisk) { Remove-Item $TargetDisk; Write-Host 'Test disk deleted.' } else { Write-Host 'No test disk.' }; Pause-Key }
      'Q' { exit 0 }
    }
  } catch { Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red; Pause-Key }
}
