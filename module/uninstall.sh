# s23-ultra-tweaks: runs when the module is removed (KernelSU runs it in the boot that removes the
# module). Starts tweaks.sh cleanup from a copy in /dev, detached, because the module folder is deleted
# right after this returns and the cleanup has to wait for Android and the first unlock. The cleanup
# undoes what outlives a reboot (Play services flags, Language Core, the Camera setting, cached app
# manifests, dual5g's modem file) and deletes /data/adb/s23tweaks; the mounts are gone after the reboot anyway.
# If the computer installer also put its own copy in /data/adb/s23tweaks, that copy is still in use:
# leave everything to it.
MODDIR=${0%/*}
[ -f /data/adb/s23tweaks/tweaks.sh ] && exit 0
cp "$MODDIR/tweaks.sh" /dev/s23tweaks_cleanup.sh || exit 0
cp "$MODDIR/files/efstool" /dev/s23tweaks_efstool 2>/dev/null && chmod 755 /dev/s23tweaks_efstool   # dual5g restore needs it
unset ASH_STANDALONE
/system/bin/nsenter -t 1 -m -- /system/bin/setsid /system/bin/sh /dev/s23tweaks_cleanup.sh cleanup </dev/null >/dev/null 2>&1 &
