#!/bin/bash
# Manual AAR + sample APK packaging (no Gradle daemon needed).
# Same inputs Gradle would use: the Java sources, the AndroidManifest and
# the per-ABI libvideomp3.so files from aar-staging/jniLibs.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export JAVA_HOME="$HOME/tools/jdk17"
export PATH="$JAVA_HOME/bin:$PATH"
JAVAC="$JAVA_HOME/bin/javac"
JAR="$JAVA_HOME/bin/jar"
KEYTOOL="$JAVA_HOME/bin/keytool"
SDK="$HOME/tools/android-sdk"
ANDROID_JAR="$SDK/platforms/android-36/android.jar"
BT="$SDK/build-tools/36.1.0"
OUT="$ROOT/dist"
WORK="$ROOT/packaging"
rm -rf "$WORK" "$OUT"
mkdir -p "$WORK/classes" "$OUT"

echo "=== compile library classes (Java 8) ==="
# shellcheck disable=SC2046
"$JAVAC" -encoding UTF-8 --release 8 -d "$WORK/classes" \
    $(find java -name '*.java')
cd "$WORK/classes"
"$JAR" cf "$WORK/classes.jar" .
cd "$ROOT"
"$JAVA_HOME/bin/javap" -classpath "$WORK/classes.jar" \
    com.kaalsoft.videomp3.VideoToMp3 | head -5

echo "=== assemble AAR ==="
mkdir -p "$WORK/aar/jni" "$WORK/aar/META-INF/com/android/build/gradle"
cp aar-project/videomp3/AndroidManifest.xml "$WORK/aar/AndroidManifest.xml"
# manual AAR manifests carry the package + minSdk (no Gradle namespace step)
cat > "$WORK/aar/AndroidManifest.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.kaalsoft.videomp3">
    <uses-sdk android:minSdkVersion="21" android:targetSdkVersion="36" />
</manifest>
EOF
cp "$WORK/classes.jar" "$WORK/aar/classes.jar"
cp -r aar-staging/jniLibs/* "$WORK/aar/jni/"
cp aar-project/videomp3/consumer-rules.pro "$WORK/aar/proguard.txt"
touch "$WORK/aar/R.txt"
cat > "$WORK/aar/META-INF/com/android/build/gradle/aar-metadata.properties" <<'EOF'
aarFormatVersion=1.0
aarMetadataVersion=1.0
minCompileSdk=21
minCompileSdkExtension=0
minAgpVersion=7.0
coreLibraryDesugaringEnabled=false
EOF
cd "$WORK/aar"
zip -q -r "$OUT/videomp3-release.aar" .
cd "$ROOT"
unzip -l "$OUT/videomp3-release.aar"

echo "=== build sample APK ==="
mkdir -p "$WORK/sample/classes" "$WORK/sample/dex" "$WORK/sample/apk"
"$JAVAC" -encoding UTF-8 --release 8 -cp "$ANDROID_JAR:$WORK/classes.jar" \
    -d "$WORK/sample/classes" \
    aar-project/sample/src/main/java/com/kaalsoft/videomp3/sample/MainActivity.java
# dex: sample classes + library classes together
cp -r "$WORK/classes/." "$WORK/sample/classes/"
"$BT/d8" --min-api 21 --lib "$ANDROID_JAR" --output "$WORK/sample/dex" \
    $(find "$WORK/sample/classes" -name '*.class')
# aapt2 needs a package attribute (Gradle would inject the namespace)
sed 's|<manifest xmlns:android="http://schemas.android.com/apk/res/android">|<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.kaalsoft.videomp3.sample">|' \
    aar-project/sample/src/main/AndroidManifest.xml > "$WORK/sample/AndroidManifest.xml"
"$BT/aapt2" link -o "$WORK/sample/apk/base.apk" \
    -I "$ANDROID_JAR" \
    --manifest "$WORK/sample/AndroidManifest.xml" \
    --min-sdk-version 21 --target-sdk-version 36 \
    --version-code 3 --version-name 2.2
cd "$WORK/sample/apk"
cp base.apk sample-unsigned.apk
cp "$WORK/sample/dex/classes.dex" .
for abi in $(ls "$ROOT/aar-staging/jniLibs"); do
    mkdir -p "lib/$abi"
    cp "$ROOT/aar-staging/jniLibs/$abi/libvideomp3.so" "lib/$abi/"
done
zip -q sample-unsigned.apk classes.dex
zip -q -r sample-unsigned.apk lib
rm -rf lib classes.dex base.apk
cd "$ROOT"
"$BT/zipalign" -f -p 4 "$WORK/sample/apk/sample-unsigned.apk" \
    "$WORK/sample/apk/sample-aligned.apk"
if [ ! -f "$WORK/debug.keystore" ]; then
    "$KEYTOOL" -genkeypair -keystore "$WORK/debug.keystore" \
        -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Kaalsoft,C=BD" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$WORK/debug.keystore" --ks-pass pass:android \
    --key-pass pass:android --out "$OUT/VideoToMp3-Sample.apk" \
    "$WORK/sample/apk/sample-aligned.apk"
"$BT/apksigner" verify --print-certs "$OUT/VideoToMp3-Sample.apk" | head -3
echo "PACKAGE OK"
ls -l "$OUT"
