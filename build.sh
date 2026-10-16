#!/usr/bin/env bash
# HomeServer OS builder: Linux + BusyBox + TUI/network apps -> UEFI-bootable disk image with an installer.
#   sudo ./build.sh                 build everything          (default stage: all)
#   sudo ./build.sh kernel rootfs   run selected stages only  (finished stages are skipped on re-runs)
set -Eeuo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
for f in "$HERE"/scripts/[0-9]*.sh; do
  # shellcheck disable=SC1090
  source "$f"
done

STAGES_ALL=(kernel busybox tui e2fs dropbear curl browser tools gl rootfs image)

usage(){ echo "usage: $0 [all | ${STAGES_ALL[*]}]"; echo "env: WORK OUT IMG IMG_SIZE ESP_MIB WITH_3D WITH_VIM WITH_RSYNC KSERIES BBVER JOBS"; }

main(){
  [[ ${1:-} =~ ^(-h|--help)$ ]] && { usage; exit 0; }
  [[ $EUID -eq 0 ]] || die "run as root (device nodes are created in the image)"
  [[ $# -gt 0 ]] || set -- all
  local s x
  for s in "$@"; do
    if [[ $s == all ]]; then
      for x in "${STAGES_ALL[@]}"; do "stage_$x"; done
    elif declare -F "stage_$s" >/dev/null; then "stage_$s"
    else die "unknown stage: $s ($(usage))"; fi
  done
}
main "$@"
