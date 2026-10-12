#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 1; }
apt-get update && apt-get install -y build-essential g++ git pkg-config bc bison flex libssl-dev libelf-dev curl xz-utils bzip2 gdisk dosfstools e2fsprogs util-linux grub-common grub2-common grub-efi-amd64-bin mtools udev
