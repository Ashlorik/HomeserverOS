# shellcheck shell=bash
# Common settings and helpers (sourced by build.sh)

KSERIES=${KSERIES:-6.12}
BBVER=${BBVER:-1.36.1}
WORK=$(realpath -m "${WORK:-$PWD/build}")
OUT=$(realpath -m "${OUT:-$PWD}")
IMG=$(realpath -m "${IMG:-$OUT/homeserver.img}")
IMG_SIZE=${IMG_SIZE:-auto}            # e.g. 1536M, or "auto"
ESP_MIB=${ESP_MIB:-128}
WITH_3D=${WITH_3D:-auto}              # auto | yes | no   (Mesa runtime copied from this host)
WITH_VIM=${WITH_VIM:-yes}
WITH_RSYNC=${WITH_RSYNC:-yes}
JOBS=${JOBS:-$(nproc)}

# pinned upstream versions (git tags)
NCURSES_REF=${NCURSES_REF:-v6_5_20251227}
DIALOG_REF=${DIALOG_REF:-t20260721}
BTOP_REF=${BTOP_REF:-v1.3.2}
GPM_REF=${GPM_REF:-master}
NNN_REF=${NNN_REF:-v5.3}
E2FS_REF=${E2FS_REF:-v1.47.4}
DROPBEAR_REF=${DROPBEAR_REF:-DROPBEAR_2026.94}
CURL_REF=${CURL_REF:-curl-8_22_0}
LIBEVENT_REF=${LIBEVENT_REF:-release-2.1.12-stable}
TMUX_REF=${TMUX_REF:-3.8}
ELINKS_REF=${ELINKS_REF:-v0.20.0}
LYNX_REF=${LYNX_REF:-v2-9-3a}
VIM_REF=${VIM_REF:-v9.2.1178}
RSYNC_REF=${RSYNC_REF:-v3.5.1}
MESON_REF=${MESON_REF:-1.12.1}

SRC=$WORK/src PKG=$WORK/pkg DEPS=$WORK/deps OUTD=$WORK/out LOGS=$WORK/logs
SRC_DIR=$HERE   # package directory (overlay/, src/)

log(){ printf '\033[1;32m[+] %s\033[0m\n' "$*"; }
warn(){ printf '\033[1;33m[!] %s\033[0m\n' "$*" >&2; }
die(){ printf '\033[1;31m[x] %s\033[0m\n' "$*" >&2; exit 1; }
need(){ local c; for c in "$@"; do command -v "$c" >/dev/null 2>&1 || die "missing tool: $c (run ./prereqs.sh)"; done; }
# A stage counts as done only if its script (and, for tools, its C sources) are unchanged since it ran; old empty markers stay valid.
stage_hash(){ local f=(); for x in "$SRC_DIR"/scripts/[0-9]*-"$1".sh; do [[ -f $x ]] && f+=("$x"); done
  [[ $1 == tools ]] && f+=("$SRC_DIR"/src/*.c); [[ ${#f[@]} -gt 0 ]] && cat "${f[@]}" | sha256sum | cut -c1-16; return 0; }
done_p(){ [[ -f $PKG/$1/.done ]] || return 1; local have; have=$(cat "$PKG/$1/.done"); [[ -z $have || $have == "$(stage_hash "$1")" ]]; }
mark(){ mkdir -p "$PKG/$1"; stage_hash "$1" >"$PKG/$1/.done" || : >"$PKG/$1/.done"; }
trap 'die "failed at ${BASH_SOURCE[0]##*/}:$LINENO (command: $BASH_COMMAND)"' ERR

mkdir -p "$SRC" "$PKG" "$DEPS" "$OUTD" "$LOGS"

# gitsrc NAME URL REF  -> $SRC/NAME (shallow clone of a tag/branch)
gitsrc(){
  local n=$1 u=$2 r=$3 d=$SRC/$1
  [[ -d $d/.git ]] && return 0
  rm -rf "$d"
  GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "$r" "$u" "$d" 2>/dev/null || true
  [[ -d $d/.git ]] || die "git clone failed: $u ($r)"
}

export LINKS_VER WORK SRC PKG DEPS OUTD LOGS JOBS HERE SRC_DIR NCURSES_REF DIALOG_REF GPM_REF BTOP_REF NNN_REF E2FS_REF DROPBEAR_REF CURL_REF \
  LIBEVENT_REF TMUX_REF ELINKS_REF LYNX_REF VIM_REF RSYNC_REF MESON_REF BBVER

# sh_step NAME < script : run a build step in its own strict bash process (so "set -e" really applies),
# keep the full log, show its tail when it fails
sh_step(){
  local name=$1 f="$LOGS/$1.sh"
  { echo 'set -Eeuo pipefail'; cat; } >"$f"
  if ! bash "$f" >"$LOGS/$name.log" 2>&1; then
    tail -n 40 "$LOGS/$name.log" >&2; die "step '$name' failed (full log: $LOGS/$name.log)"
  fi
}

# stable random identifiers, generated once per work directory
ids(){
  local f=$OUTD/ids.env
  if [[ ! -f $f ]]; then
    printf 'LIVE_GUID=%s\nINST_GUID=%s\nESP_GUID=%s\n' \
      "$(cat /proc/sys/kernel/random/uuid)" "$(cat /proc/sys/kernel/random/uuid)" "$(cat /proc/sys/kernel/random/uuid)" >"$f"
  fi
  # shellcheck disable=SC1090
  . "$f"
}

# console look: dark palette (Catppuccin-like), quiet boot, no logo
THEME_ARGS="quiet logo.nologo vt.default_red=0x1e,0xc4,0x4f,0xc5,0x3b,0x8e,0x3d,0xb4,0x6c,0xf3,0xa6,0xf9,0x89,0xcb,0x94,0xf5 vt.default_grn=0x1e,0x4f,0x9d,0x9d,0x5f,0x5f,0x9a,0xbc,0x70,0x8b,0xe3,0xe2,0xb4,0xa6,0xe2,0xf7 vt.default_blu=0x2e,0x6b,0x69,0x4a,0xa8,0xb0,0xa8,0xe0,0x86,0xa8,0xa1,0xaf,0xfa,0xf7,0xd5,0xff"
cmdline(){ printf 'root=PARTUUID=%s rootfstype=ext4 rootwait rw init=/init console=ttyS0,115200 console=tty0 loglevel=3 %s' "$1" "$THEME_ARGS"; }
