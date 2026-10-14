# shellcheck shell=bash
# Static applications: e2fsprogs (installer), dropbear (SSH), curl, lynx (web browser)

stage_e2fs(){
  done_p e2fs && { log "e2fs: done (skipping)"; return 0; }
  gitsrc e2fsprogs https://github.com/tytso/e2fsprogs "$E2FS_REF"
  log "e2fsprogs $E2FS_REF"
  sh_step e2fs <<'EOF'
P=$PKG/e2fs; rm -rf "$P" "$SRC/e2fs-b"; mkdir -p "$P/usr/sbin" "$SRC/e2fs-b"; cd "$SRC/e2fs-b"
LDFLAGS=-static "$SRC/e2fsprogs/configure" --disable-nls --disable-fuse2fs --disable-uuidd --disable-debugfs --disable-imager \
  --disable-defrag --disable-e2initrd-helper --disable-libblkid --disable-libuuid
make -j"$JOBS"
for f in misc/mke2fs e2fsck/e2fsck misc/tune2fs resize/resize2fs; do strip -o "$P/usr/sbin/${f##*/}" "$f"; done
file "$P"/usr/sbin/* | cut -c1-100
EOF
  mark e2fs
}

stage_dropbear(){
  done_p dropbear && { log "dropbear: done (skipping)"; return 0; }
  gitsrc dropbear https://github.com/mkj/dropbear "$DROPBEAR_REF"
  log "dropbear $DROPBEAR_REF"
  sh_step dropbear <<'EOF'
P=$PKG/dropbear; rm -rf "$P"; mkdir -p "$P/usr/bin"; cd "$SRC/dropbear"; make distclean >/dev/null 2>&1 || true
./configure --enable-static --disable-pam --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx --disable-pututline --disable-pututxline
make -j"$JOBS" PROGRAMS="dropbear dropbearkey dbclient scp" MULTI=1 STATIC=1
strip -o "$P/usr/bin/dropbearmulti" dropbearmulti
file "$P/usr/bin/dropbearmulti" | cut -c1-100
EOF
  mark dropbear
}

stage_curl(){
  done_p curl && { log "curl: done (skipping)"; return 0; }
  need cmake
  gitsrc curl https://github.com/curl/curl "$CURL_REF"
  log "curl $CURL_REF"
  sh_step curl <<'EOF'
P=$PKG/curl; rm -rf "$P" "$SRC/curl-b"; mkdir -p "$P/usr/bin"
cmake -S "$SRC/curl" -B "$SRC/curl-b" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_STATIC_CURL=ON -DCURL_USE_OPENSSL=ON \
  -DOPENSSL_USE_STATIC_LIBS=ON -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=none -DCURL_USE_LIBPSL=OFF -DUSE_NGHTTP2=OFF \
  -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_DISABLE_LDAP=ON -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF \
  -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static -DZLIB_LIBRARY=/usr/lib/x86_64-linux-gnu/libz.a
cmake --build "$SRC/curl-b" -j"$JOBS"
strip -o "$P/usr/bin/curl" "$SRC/curl-b/src/curl"
file "$P/usr/bin/curl" | cut -c1-100
EOF
  mark curl
}

stage_browser(){
  done_p browser && { log "browser: done (skipping)"; return 0; }
  stage_tui   # needs the static ncurses from $DEPS
  gitsrc lynx https://github.com/ThomasDickey/lynx-snapshots "$LYNX_REF"
  log "lynx $LYNX_REF"
  sh_step lynx <<'EOF'
D=$DEPS; P=$PKG/browser; rm -rf "$P" "$SRC/lynx-b"; mkdir -p "$P/usr/bin" "$SRC/lynx-b"; cd "$SRC/lynx-b"
export PKG_CONFIG_PATH=$D/lib/pkgconfig
CPPFLAGS="-I$D/include" LDFLAGS="-static -L$D/lib" LIBS="-lssl -lcrypto -ldl -lpthread -lz" \
  "$SRC/lynx/configure" --prefix=/usr --with-ssl --with-screen=ncursesw --with-zlib --disable-nls --disable-echo \
  --enable-default-colors --enable-color-style --with-ssl-compat 2>/dev/null || \
CPPFLAGS="-I$D/include" LDFLAGS="-static -L$D/lib" LIBS="-lssl -lcrypto -ldl -lpthread -lz" \
  "$SRC/lynx/configure" --prefix=/usr --with-ssl --with-screen=ncursesw --with-zlib --disable-nls --enable-default-colors
make -j"$JOBS"
strip -o "$P/usr/bin/lynx" lynx
file "$P/usr/bin/lynx" | cut -c1-100
EOF
  mark browser
}
