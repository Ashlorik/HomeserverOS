# shellcheck shell=bash
# Optional 3D support: copy the Mesa OpenGL/EGL runtime (software llvmpipe + GPU drivers) and its libraries from this host.
# Raw support: no windowing system; used for off-screen rendering (EGL surfaceless / GBM), tested with hs-glinfo.

gl_host_ok(){ local L=/usr/lib/x86_64-linux-gnu
  [[ -e $L/libEGL.so.1 && -e $L/libGLESv2.so.2 && -e $L/libEGL_mesa.so.0 && -e $L/libgbm.so.1 ]] && compgen -G "$L/libgallium-*.so" >/dev/null; }

stage_gl(){
  done_p gl && { log "gl: done (skipping)"; return 0; }
  if [[ $WITH_3D == no ]] || { [[ $WITH_3D == auto ]] && ! gl_host_ok; }; then
    warn "gl: 3D runtime skipped (WITH_3D=$WITH_3D; Mesa runtime installed on this host: $(gl_host_ok && echo yes || echo no))"
    mark gl; touch "$PKG/gl/.skipped"; return 0
  fi
  gl_host_ok || die "WITH_3D=yes but the Mesa runtime is missing (apt install libegl1 libgles2 libgbm1 libgl1-mesa-dri libegl-mesa0)"
  need ldd
  log "gl: collecting the Mesa runtime"
  local L=/usr/lib/x86_64-linux-gnu P=$PKG/gl dst seeds f real n
  rm -rf "$P"; dst=$P/usr/lib/x86_64-linux-gnu; mkdir -p "$dst" "$P/usr/lib64" "$P/usr/lib/hs-gl/dri" "$P/usr/lib/hs-gl/gbm" "$P/usr/share/hs-gl"
  seeds=("$L/libEGL.so.1" "$L/libGLESv2.so.2" "$L/libGLdispatch.so.0" "$L/libEGL_mesa.so.0" "$L/libgbm.so.1" "$L"/libgallium-*.so "$L/dri/libdril_dri.so" "$L/gbm/dri_gbm.so" "$PKG/tools/usr/bin/hs-glinfo.bin")
  while read -r n real; do          # soname + real path of the whole dependency closure
    [[ -n $real && -e $real ]] || continue
    [[ -e $dst/$n ]] || cp -L "$real" "$dst/$n"
  done < <(for f in "${seeds[@]}"; do ldd "$f" 2>/dev/null | awk '/=>/ && $3 ~ /^\// {print $1, $3}'; done | sort -u)
  for f in "${seeds[@]}"; do case $f in */gbm/*|*/dri/*|*hs-glinfo*) ;; *) [[ -e $dst/${f##*/} ]] || cp -L "$f" "$dst/${f##*/}";; esac; done
  cp -L /lib64/ld-linux-x86-64.so.2 "$P/usr/lib64/"
  cp -a "$L"/dri/. "$P/usr/lib/hs-gl/dri/"; cp -L "$L/gbm/dri_gbm.so" "$P/usr/lib/hs-gl/gbm/"
  printf '{"file_format_version":"1.0.0","ICD":{"library_path":"libEGL_mesa.so.0"}}\n' >"$P/usr/share/hs-gl/50_mesa.json"
  strip --strip-unneeded "$dst"/*.so* 2>/dev/null || true
  log "gl: runtime $(du -sh "$P" | cut -f1) (uncompressed)"
  mark gl
}
