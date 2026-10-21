#!/bin/sh
. /usr/lib/hs/common.sh
d=$(dialog --stdout --inputbox "Where to save the backup? (folder, e.g. /mnt/usb)" 8 60 /root) || exit 0
mkdir -p "$d" || exit 1
f="$d/hs-backup-$(hostname)-$(date +%Y%m%d-%H%M).tar.gz"
tar -czf "$f" -C / etc/hs etc/passwd etc/shadow etc/group root home 2>/dev/null && echo "Saved: $f ($(du -h "$f" | cut -f1))" || echo "Backup failed"
echo; echo "Restore: tar -xzf FILE -C /"; read -r _
