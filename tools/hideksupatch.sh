#!/usr/bin/env bash
# Make the hideksu payload for a firmware's /system/framework/services.jar (macOS or Linux).
#   tools/hideksupatch.sh <stock services.jar> <out dir>
# Writes <out dir>/hideksu-<first 8 of the stock md5>.tar.gz (holding services.jar) and prints the line
# for HIDEKSU_JARS and the one for PAYLOADS in device/tweaks.sh. Needs apktool (3.x) and Java.
#
# The patch: add src/hideksu/H.smali (s23tweaks/H.f: true when the package is the KernelSU manager and the
# caller is an app, appId >= 10000, other than df.root, the manager itself or the Samsung launcher) and call
# it at the top of AppsFilterBase.shouldFilterApplicationInternal, returning "filtered" when it says so.
# apktool's minSdkVersion is lowered to 29 so smali writes plain classes*.dex, not a dex 041 container.
set -euo pipefail
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }
[ $# = 2 ] || { sed -n '2,4p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
ROOT=$(cd "$(dirname "$0")/.." && pwd); IN=$1; OUT=$2
command -v apktool >/dev/null || die "apktool not found"
if command -v md5sum >/dev/null; then md5() { md5sum "$1" | cut -d' ' -f1; }; else md5() { command md5 -q "$1"; }; fi   # macOS: without command this calls itself
if command -v sha256sum >/dev/null; then sha() { sha256sum "$1" | cut -d' ' -f1; }; else sha() { shasum -a 256 "$1" | cut -d' ' -f1; }; fi
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

apktool d -q -f -o "$W/d" "$IN" || die "apktool couldn't decode $IN"
sed 's/^\(  minSdkVersion: \).*/\129/' "$W/d/apktool.yml" > "$W/yml" && mv "$W/yml" "$W/d/apktool.yml"
f=$(cd "$W/d" && ls smali*/com/android/server/pm/AppsFilterBase.smali 2>/dev/null | head -n 1)
[ -n "$f" ] || die "no com/android/server/pm/AppsFilterBase in $IN"
mkdir -p "$W/d/${f%%/*}/s23tweaks"; cp "$ROOT/src/hideksu/H.smali" "$W/d/${f%%/*}/s23tweaks/H.smali"
SIG='.method public final shouldFilterApplicationInternal(Lcom/android/server/pm/Computer;ILjava/lang/Object;Lcom/android/server/pm/pkg/PackageStateInternal;I)Z'
# after the method's .locals line: v0 is a local the original code always writes before reading
awk -v sig="$SIG" '
  $0 == sig { m = 1 }
  { print }
  m && /^    \.locals [1-9]/ {
    print ""
    print "    invoke-static {p1, p2, p4}, Ls23tweaks/H;->f(Lcom/android/server/pm/Computer;ILcom/android/server/pm/pkg/PackageStateInternal;)Z"
    print ""; print "    move-result v0"; print ""; print "    if-eqz v0, :s23t_hideksu"; print ""
    print "    const/4 v0, 0x1"; print ""; print "    return v0"; print ""; print "    :s23t_hideksu"
    m = 0; n++ }
  END { if (n != 1) exit 1 }' "$W/d/$f" > "$W/afb" || die "shouldFilterApplicationInternal (with .locals >= 1) not found in $f"
mv "$W/afb" "$W/d/$f"
apktool b -q -o "$W/services.jar" "$W/d" || die "apktool couldn't rebuild the jar"

stock=$(md5 "$IN"); patched=$(md5 "$W/services.jar"); mkdir -p "$OUT"
T=$OUT/hideksu-${stock:0:8}.tar.gz
(cd "$W" && COPYFILE_DISABLE=1 tar -czf "$T" services.jar)
echo "Built $T"
echo "HIDEKSU_JARS line: $stock $patched <firmware it came from>"
echo "PAYLOADS line:     hideksu:$stock ${T##*/} $(sha "$T")"
