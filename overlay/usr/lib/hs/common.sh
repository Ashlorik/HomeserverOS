# HomeServer OS shared helpers (sourced by hs-* scripts)
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/bin
export DIALOGRC=/etc/dialogrc LANG=C.UTF-8 LC_ALL=C.UTF-8 LOCPATH=/usr/lib/locale ESCDELAY=200
[ -r /etc/hs/theme.dialogrc ] && export DIALOGRC=/etc/hs/theme.dialogrc
case ${TERM:-} in ''|dumb|unknown) TERM=linux;; esac
export TERM
HS=/etc/hs
[ -r $HS/tz ] && { TZ=$(cat $HS/tz); export TZ; }
ESC=$(printf '\033')
c(){ printf '%s[%sm' "$ESC" "$1"; }
ok(){ printf ' %s[  OK  ]%s %s\n' "$(c '1;32')" "$(c 0)" "$*"; }
warn(){ printf ' %s[ WARN ]%s %s\n' "$(c '1;33')" "$(c 0)" "$*"; }
fail(){ printf ' %s[FAILED]%s %s\n' "$(c '1;31')" "$(c 0)" "$*"; }
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
