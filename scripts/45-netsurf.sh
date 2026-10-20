# shellcheck shell=bash
# NetSurf (framebuffer front end): a real graphical web browser (HTML, CSS, images, https) that draws straight on /dev/fb0 - no X11 or Wayland needed.
NETSURF_VER=${NETSURF_VER:-3.11}

stage_netsurf(){
  done_p netsurf && { log "netsurf: done (skipping)"; return 0; }
  need gcc make curl flex bison perl pkg-config
  log "netsurf $NETSURF_VER (framebuffer)"
  local P=$PKG/netsurf; rm -rf "$P"; mkdir -p "$P"
  local tb=$SRC/netsurf-all-$NETSURF_VER.tar.gz
  [[ -f $tb ]] || curl -fL --retry 3 -m 300 -o "$tb" "https://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-$NETSURF_VER.tar.gz" || die "netsurf download failed"
  sh_step netsurf <<'EOF'
rm -rf "$SRC/netsurf-all"; mkdir -p "$SRC/netsurf-all"; tar -xzf "$SRC/netsurf-all-$NETSURF_VER.tar.gz" -C "$SRC/netsurf-all" --strip-components=1
cd "$SRC/netsurf-all"
cat > netsurf/Makefile.config <<'CFG'
override NETSURF_USE_JS := NO
override NETSURF_USE_WEBP := NO
override NETSURF_USE_VIDEO := NO
override NETSURF_USE_NSSVG := NO
override NETSURF_USE_RSVG := NO
override NETSURF_USE_ROSPRITE := NO
override NETSURF_FB_FRONTEND := linux
override NETSURF_FB_FONTLIB := internal
override NETSURF_FB_RESPATH := /usr/share/netsurf/
CFG
make TARGET=framebuffer PREFIX=/usr -j"$JOBS"
make TARGET=framebuffer PREFIX=/usr DESTDIR="$PKG/netsurf" install
find "$PKG/netsurf" -type f | head -30
EOF
  [[ -x $P/usr/bin/netsurf-fb ]] || die "netsurf-fb was not built"
  strip "$P/usr/bin/netsurf-fb" || true
  # shared libraries the binary needs that are not part of the base system
  mkdir -p "$P/usr/lib"
  ldd "$P/usr/bin/netsurf-fb" | awk '/=> \//{print $3}' | while read -r l; do cp -Ln "$l" "$P/usr/lib/" 2>/dev/null || true; done
  mark netsurf
}
