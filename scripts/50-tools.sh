# shellcheck shell=bash
# Small helper programs written for this project

stage_tools(){
  done_p tools && { log "tools: done (skipping)"; return 0; }
  need gcc
  log "tools: hs-gpt (static), hs-glinfo"
  local P=$PKG/tools; rm -rf "$P"; mkdir -p "$P/usr/sbin" "$P/usr/bin"
  gcc -O2 -Wall -static -o "$P/usr/sbin/hs-gpt" "$SRC_DIR/src/hs-gpt.c"
  strip "$P/usr/sbin/hs-gpt"
  gcc -O2 -Wall -o "$P/usr/bin/hs-glinfo.bin" "$SRC_DIR/src/hs-glinfo.c" -ldl   # dynamic: runs only with the 3D runtime
  strip "$P/usr/bin/hs-glinfo.bin"
  mark tools
}
