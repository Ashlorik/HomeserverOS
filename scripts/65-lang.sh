# shellcheck shell=bash
# Russian support for the text console: a PSF console font with Cyrillic (loaded with setfont) and a keyboard map (compiled with loadkeys -b for BusyBox loadkmap)
stage_lang(){
  done_p lang && { log "lang: done (skipping)"; return 0; }
  need loadkeys gzip
  log "lang: Cyrillic console font + Russian keyboard map"
  local P=$PKG/lang; rm -rf "$P"; mkdir -p "$P/usr/share/hs/fonts" "$P/usr/share/hs/kmap"
  local f m
  for f in /usr/share/consolefonts/Uni2-Terminus16.psf.gz /usr/share/consolefonts/Uni2-VGA16.psf.gz /usr/share/consolefonts/Cyr_a8x16.psf.gz /usr/share/consolefonts/cyr-sun16.psf.gz; do
    [[ -f $f ]] && break; f=; done
  [[ -n $f ]] || die "no Cyrillic console font found (apt install console-setup-linux kbd)"
  gzip -dc "$f" >"$P/usr/share/hs/fonts/cyr.psf"; log "lang: font $(basename "$f")"
  m=$(find /usr/share/keymaps /usr/share/kbd/keymaps -name 'ruwin_alt-UTF-8.map*' 2>/dev/null | head -n1)
  [[ -n $m ]] || m=$(find /usr/share/keymaps /usr/share/kbd/keymaps -name 'ruwin_ct_sh-UTF-8.map*' -o -name 'ru.map*' 2>/dev/null | head -n1)
  [[ -n $m ]] || die "no Russian keymap found (apt install kbd)"
  loadkeys -u -b "$m" >"$P/usr/share/hs/kmap/ru.bmap" || die "loadkeys -b failed for $m"
  m=$(find /usr/share/keymaps /usr/share/kbd/keymaps -name 'us.map*' -o -name 'us-acentos.map*' 2>/dev/null | head -n1)
  [[ -n $m ]] && loadkeys -u -b "$m" >"$P/usr/share/hs/kmap/us.bmap" || true
  log "lang: keymap ready ($(du -sh "$P/usr/share/hs" | cut -f1))"
  mark lang
}
