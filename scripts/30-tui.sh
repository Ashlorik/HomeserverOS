# shellcheck shell=bash
# ncurses (static, kept in $DEPS for other programs) + dialog, nnn, btop

stage_tui(){
  done_p tui && { log "tui: done (skipping)"; return 0; }
  need git make gcc g++ pkg-config
  log "tui: ncurses, dialog, nnn, btop"
  gitsrc ncurses https://github.com/ThomasDickey/ncurses-snapshots "$NCURSES_REF"
  gitsrc dialog  https://github.com/ThomasDickey/dialog-snapshots  "$DIALOG_REF"
  gitsrc nnn     https://github.com/jarun/nnn                      "$NNN_REF"
  gitsrc btop    https://github.com/aristocratos/btop              "$BTOP_REF"
  sh_step tui-ncurses <<'EOF'
D=$DEPS; rm -rf "$SRC/ncurses-b"; mkdir -p "$D" "$SRC/ncurses-b"; cd "$SRC/ncurses-b"
"$SRC/ncurses/configure" --prefix="$D" --without-shared --with-normal --without-debug --without-ada --without-cxx --without-cxx-binding \
  --without-manpages --without-tests --enable-widec --enable-pc-files --with-pkg-config-libdir="$D/lib/pkgconfig" --enable-overwrite \
  --disable-database --with-fallbacks=linux,vt100,vt220,xterm,xterm-256color,screen,screen-256color,tmux,tmux-256color
make -j"$JOBS"
make install
EOF
  sh_step tui-apps <<'EOF'
D=$DEPS; P=$PKG/tui; rm -rf "$P" "$SRC/dialog-b"; mkdir -p "$P/usr/bin" "$P/usr/share/btop" "$SRC/dialog-b"
export PKG_CONFIG_PATH=$D/lib/pkgconfig PKG_CONFIG_LIBDIR=$D/lib/pkgconfig
cd "$SRC/dialog-b"
LDFLAGS="-static -L$D/lib" CPPFLAGS="-I$D/include" "$SRC/dialog/configure" --prefix="$D" --with-ncursesw --disable-nls --with-pkg-config
make -j"$JOBS"
strip -o "$P/usr/bin/dialog" dialog
cd "$SRC/nnn"; make clean >/dev/null 2>&1 || true
make -j"$JOBS" O_NORL=1 O_NOMOUSE=0 STATIC=1 CFLAGS_CURSES="$(pkg-config ncursesw --cflags)" LDLIBS_CURSES="$(pkg-config ncursesw --libs --static)"
strip -o "$P/usr/bin/nnn" nnn
cd "$SRC/btop"; make clean >/dev/null 2>&1 || true
make -j"$JOBS" STATIC=true GPU_SUPPORT=false
strip -o "$P/usr/bin/btop" bin/btop
cp -a themes "$P/usr/share/btop/"
file "$P"/usr/bin/* | cut -c1-110
EOF
  mark tui
}
