# shellcheck shell=bash
# Links 2 in graphics mode: a real graphical web browser (HTML, tables, images, https) that draws straight on the framebuffer /dev/fb0
# (no X11/Wayland); the mouse comes from gpm. No JavaScript.
LINKS_VER=${LINKS_VER:-2.30}

stage_links(){
  done_p links && { log "links: done (skipping)"; return 0; }
  need gcc make curl pkg-config
  log "links $LINKS_VER (graphics, framebuffer)"
  local P=$PKG/links; rm -rf "$P"; mkdir -p "$P"
  local tb=$SRC/links-$LINKS_VER.tar.gz v
  if [[ ! -f $tb ]]; then
    for v in "$LINKS_VER" 2.30 2.29 2.28; do
      curl -fL --retry 2 -m 200 -o "$tb" "http://links.twibright.com/download/links-$v.tar.gz" 2>/dev/null && break; rm -f "$tb"; done
    [[ -f $tb ]] || die "links download failed"
  fi
  export LINKS_TB=$tb
  sh_step links <<'EOF'
D=$DEPS; rm -rf "$SRC/links-b"; mkdir -p "$SRC/links-b"; tar -xzf "$LINKS_TB" -C "$SRC/links-b" --strip-components=1; cd "$SRC/links-b"
CPPFLAGS="-I$D/include" LDFLAGS="-L$D/lib" LIBS="-lm" ./configure --prefix=/usr --enable-graphics --with-fb --without-x --without-svgalib --without-directfb \
  --without-sdl --without-libtiff --without-librsvg --without-libevent --with-ssl --with-libjpeg --disable-javascript 2>&1 | tail -25
make -j"$JOBS"
make DESTDIR="$PKG/links" install
EOF
  [[ -x $P/usr/bin/links ]] || die "links was not built"
  strip "$P/usr/bin/links" || true; rm -rf "$P/usr/share/man"
  mkdir -p "$P/usr/lib"
  ldd "$P/usr/bin/links" | awk '/=> \//{print $3}' | while read -r l; do case $(basename "$l") in libc.so*|libm.so*|libresolv.so*|libdl.so*|libpthread.so*|ld-linux*) ;; *) cp -Ln "$l" "$P/usr/lib/" 2>/dev/null || true;; esac; done
  mark links
}
