#!/bin/sh
# Builds device/files/aicore-stub.apk. Needs the Android SDK (build-tools 37, platform 37.0) and a JDK.
set -e
cd "$(dirname "$0")"
SDK=${ANDROID_HOME:-$HOME/Library/Android/sdk}
BT=$SDK/build-tools/37.0.0; JAR=$SDK/platforms/android-37.0/android.jar
KS=${STUB_KEYSTORE:-$HOME/.android/debug.keystore}
O=$(mktemp -d); trap 'rm -rf $O' EXIT
javac -source 11 -target 11 -classpath $JAR -d $O/c java/com/samsung/android/aicore/*.java 2>&1 | grep -v "^warning" || true
$BT/d8 --min-api 34 --lib $JAR --output $O $(find $O/c -name '*.class')
$BT/aapt2 link -o $O/u.apk -I $JAR --manifest AndroidManifest.xml
(cd $O && zip -q u.apk classes.dex)
$BT/zipalign -f 4 $O/u.apk $O/a.apk
$BT/apksigner sign --ks $KS --ks-pass pass:android --out ../../device/files/aicore-stub.apk $O/a.apk
rm -f ../../device/files/aicore-stub.apk.idsig
ls -l ../../device/files/aicore-stub.apk
