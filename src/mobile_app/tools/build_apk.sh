#!/usr/bin/env bash
# Builds the Android APK without the Android SDK, from Maven Central artefacts:
#   apktool-lib (aapt2 + framework resources), dalvik-dx, apksig and the
#   Robolectric android-all jar (API 34 classes to compile against).
#
#   tools/build_apk.sh            -> dist/JianweiVibro-<version>.apk
# Needs: bash, curl, unzip, python3 (+ Pillow and fonttools only to regenerate
# icons/fonts), JDK 17+. Signing key: android/keystore/release.p12 (created on
# first use; keep it to install updates over an existing installation).
set -euo pipefail
export LC_ALL=C.UTF-8 LANG=C.UTF-8  # Java needs it for the non-ASCII project path
cd "$(dirname "$0")/.."
VERSION_NAME=1.0.5
VERSION_CODE=6
CACHE="${JW_APK_CACHE:-$HOME/.cache/jianwei-apk}"
M=https://repo.maven.apache.org/maven2
mkdir -p "$CACHE" build dist
fetch() { [ -s "$CACHE/$2" ] || curl -sSfL --retry 4 -o "$CACHE/$2" "$M/$1"; }
fetch org/apktool/apktool-lib/3.0.3/apktool-lib-3.0.3.jar apktool-lib.jar
fetch com/jakewharton/android/repackaged/dalvik-dx/16.0.1/dalvik-dx-16.0.1.jar dx.jar
fetch com/android/tools/build/apksig/2.3.0/apksig-2.3.0.jar apksig.jar
fetch org/robolectric/android-all/14-robolectric-10818077/android-all-14-robolectric-10818077.jar android-all.jar
if [ ! -x "$CACHE/aapt2" ]; then
  unzip -o -q -j "$CACHE/apktool-lib.jar" prebuilt/linux/aapt2 prebuilt/android-framework.jar -d "$CACHE"
  chmod +x "$CACHE/aapt2"
fi
AAPT2="$CACHE/aapt2"
rm -rf build/* && mkdir -p build/assets build/classes build/signer

# 1. resources and assets (the web app)
cp -r web build/assets/web
"$AAPT2" compile --dir android/res -o build/res.zip
"$AAPT2" link -o build/base.apk -I "$CACHE/android-framework.jar" --manifest android/AndroidManifest.xml \
  --min-sdk-version 24 --target-sdk-version 34 --version-code $VERSION_CODE --version-name $VERSION_NAME \
  -A build/assets build/res.zip

# 2. Java -> dex
javac -nowarn -source 8 -target 8 -encoding UTF-8 -cp "$CACHE/android-all.jar" -d build/classes \
  $(find android/java -name '*.java') 2>&1 | grep -v "bootstrap class path\|source value 8\|target value 8\|To suppress warnings" || true
[ -f build/classes/com/jianwei/vibro/MainActivity.class ] || { echo "javac failed"; exit 1; }
java -cp "$CACHE/dx.jar" com.android.dx.command.Main --dex --min-sdk-version=24 --output=build/classes.dex build/classes

# 3. pack, align, sign
python3 tools/apk_pack.py build/base.apk build/classes.dex build/unsigned.apk
KS=android/keystore/release.p12
if [ ! -f "$KS" ]; then
  mkdir -p android/keystore
  keytool -genkeypair -keystore "$KS" -storetype PKCS12 -storepass jianwei2026 -keypass jianwei2026 -alias jianwei \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Jianwei Vibro, O=Jianwei, C=CN" >/dev/null 2>&1
fi
javac -nowarn -cp "$CACHE/apksig.jar" -d build/signer tools/signer/ApkSign.java
OUT=dist/JianweiVibro-$VERSION_NAME.apk
java --add-exports java.base/sun.security.x509=ALL-UNNAMED --add-exports java.base/sun.security.pkcs=ALL-UNNAMED -cp "$CACHE/apksig.jar:build/signer" ApkSign build/unsigned.apk "$OUT" "$KS" jianwei2026 jianwei
echo "built $OUT ($(du -h "$OUT" | cut -f1))"
