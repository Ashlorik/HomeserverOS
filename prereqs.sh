#!/usr/bin/env bash
# Debian/Ubuntu build dependencies (run as root)
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 1; }
apt-get update
apt-get install -y build-essential g++ git pkg-config cmake bc bison flex curl xz-utils bzip2 python3 file \
  libssl-dev zlib1g-dev libelf-dev dosfstools e2fsprogs util-linux ca-certificates ncurses-bin
# optional, for the 3D runtime copied into the image (WITH_3D=auto|yes|no):
apt-get install -y libegl1 libgles2 libgbm1 libgl1-mesa-dri libegl-mesa0 || echo "3D runtime packages not installed: the image will be built without 3D"
