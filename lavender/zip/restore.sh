#!/sbin/sh
# Run from OrangeFox terminal: restores backed up boot
BOOT=/dev/block/bootdevice/by-name/boot$(getprop ro.boot.slot_suffix)
dd if=/data/media/0/HSBOOT/backup/boot.img of=$BOOT bs=1M && echo restored
