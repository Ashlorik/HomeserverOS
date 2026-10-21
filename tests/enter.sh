#!/bin/sh
# runs inside unshare: prepare a throw-away /proc, /dev, /tmp in the chroot and exec the command
R=$1; shift
mount -t proc proc $R/proc; mount -t tmpfs tmpfs $R/dev; mount -t tmpfs tmpfs $R/tmp; mount -t tmpfs tmpfs $R/run
for n in null:1:3 zero:1:5 full:1:7 random:1:8 urandom:1:9 tty:5:0 console:5:1 ptmx:5:2; do IFS=: read a b c <<EOT
$n
EOT
  mknod -m 666 $R/dev/$a c $b $c; done
mount -t sysfs sysfs $R/sys 2>/dev/null
mkdir -p $R/dev/pts; mount -t devpts devpts $R/dev/pts
[ -n "$HS_TEST_DEV" ] && for d in $HS_TEST_DEV; do cp -a $d $R/dev/; done
exec chroot $R "$@"
