#!/usr/bin/env bash
# chargewatts: build a patched SystemUI.apk or AOD (com.samsung.android.app.aodservice) APK, and the payload the
# phone needs to rebuild it.
#   tools/chargewatts/build.sh systemui SystemUI.apk <outdir>
#   tools/chargewatts/build.sh aod base.apk <outdir>
# Needs apktool (its bundled smali), a JDK, and the Android SDK (android.jar, d8).
#   systemui: one call in KeyguardSecIndicationController.addInitialIndication() (hook.py), the lock screen line,
#             and one in ChargingNotification$show$1.run() (noticehook.py), the charging notification's text
#   aod:      the AOD's time-to-full line (aodhook.py); AOD runs as a plugin inside SystemUI's process
# plus S23Watts in a new classes<N>.dex. The patched APK keeps every stock byte up to the APK Signing Block
# (mkapk.py), so the payload is just the tail: the phone rebuilds it as head -c <keep> stock + tail, checks the md5.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); KIND=$1; APK=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); O=$(mkdir -p "$3" && cd "$3" && pwd)
APKTOOL=${APKTOOL:-$(ls /opt/homebrew/Cellar/apktool/*/libexec/apktool_*.jar 2>/dev/null | sort -V | tail -n 1)}
SDK=${ANDROID_HOME:-$HOME/Library/Android/sdk}
AJ=$(ls -d "$SDK"/platforms/android-*/android.jar | sort -V | tail -n 1); D8=$(ls -d "$SDK"/build-tools/*/d8 | sort -V | tail -n 1)
PY=${PY:-python3}; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
md5() { if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

java -jar "$APKTOOL" d -r --no-assets -f -o "$W/at" "$APK" >/dev/null
mkdir -p "$W/src"
case $KIND in
  systemui)
    K=$(grep -rl --include=KeyguardSecIndicationController.smali '^\.class .*Lcom/android/systemui/statusbar/KeyguardSecIndicationController;' "$W/at" | head -n 1)
    [ -n "$K" ] || { echo "KeyguardSecIndicationController not found" >&2; exit 1; }
    "$PY" -I "$D/hook.py" "$K"; PKG=com/android/systemui/statusbar
    C=$(grep -rl --include='ChargingNotification$show$1.smali' '^\.class .*Lcom/android/systemui/power/feature/charginginfo/ChargingNotification\$show\$1;' "$W/at" | head -n 1)
    [ -n "$C" ] || { echo 'ChargingNotification$show$1 not found' >&2; exit 1; }
    "$PY" -I "$D/noticehook.py" "$C"; K="$K
$C"
    cp "$D/S23Watts.java" "$W/src/" ;;
  aod)
    K=$("$PY" -I "$D/aodhook.py" "$W/at" | tee /dev/stderr | sed -n 's/^hooked .* returns in //p'); PKG=s23tweaks
    sed 's/^package com\.android\.systemui\.statusbar;/package s23tweaks;/' "$D/S23Watts.java" > "$W/src/S23Watts.java" ;;
  *) echo "usage: build.sh systemui|aod <apk> <outdir>" >&2; exit 2 ;;
esac
javac -cp "$APKTOOL" -d "$W/asm" "$D/Asm.java"
DEX=""   # reassemble each smali_classesN that holds a hooked class -> classesN.dex
for SD in $(echo "$K" | while IFS= read -r f; do echo "$W/at/$(echo "${f#"$W"/at/}" | cut -d/ -f1)"; done | sort -u); do
  N=${SD##*/smali_classes}; [ "$N" != "$SD" ] || N=""
  java -Xmx6g -cp "$APKTOOL:$W/asm" Asm "$SD" "$W/classes$N.dex" 29
  DEX="$DEX classes$N.dex=$W/classes$N.dex"
done
javac -encoding UTF-8 --release 11 -cp "$AJ" -d "$W/cls" "$W/src/S23Watts.java"
mkdir -p "$W/helper"; "$D8" --release --min-api 30 --lib "$AJ" --output "$W/helper" "$W/cls/$PKG/S23Watts.class"
last=$(unzip -Z1 "$APK" 'classes*.dex' | sed 's/classes\([0-9]*\)\.dex/\1/' | sed 's/^$/1/' | sort -n | tail -n 1)
H=classes$((last + 1)).dex; NAME=$([ $KIND = aod ] && echo AOD || echo SystemUI)
keep=$("$PY" -I "$D/mkapk.py" "$APK" "$O/$NAME.apk" "$W/tail" $DEX "$H=$W/helper/classes.dex" | sed -n 's/^stock kept up to \([0-9]*\) .*/\1/p')
sm=$(md5 "$APK"); pm=$(md5 "$O/$NAME.apk"); echo "$KIND $sm $pm $keep" > "$O/$NAME.row"
cp "$W/tail" "$O/$NAME.tail"
echo "row: $KIND $sm $pm $keep"
