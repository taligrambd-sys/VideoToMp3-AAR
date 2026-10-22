#!/bin/bash
# Android build: LAME (-O2) + trimmed FFmpeg + libvideomp3.so for 3 ABIs.
# Usage: build-android.sh [arm64-v8a armeabi-v7a x86_64]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
NDK="$HOME/tools/android-ndk-r27c"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
API=21
JOBS=2
ABIS=("$@")
if [ ${#ABIS[@]} -eq 0 ]; then
    ABIS=(arm64-v8a armeabi-v7a x86_64)
fi

build_abi() {
    local ABI="$1"
    local TARGET HOST_TRIPLE ARCH CPU EXTRA_FF EXTRA_CFLAGS
    case "$ABI" in
        arm64-v8a)
            TARGET="aarch64-linux-android"
            HOST_TRIPLE="aarch64-linux-android"
            ARCH="aarch64"; CPU="generic"
            EXTRA_FF="--enable-asm --enable-neon"
            EXTRA_CFLAGS="" ;;
        armeabi-v7a)
            TARGET="armv7a-linux-androideabi"
            HOST_TRIPLE="arm-linux-androideabi"
            ARCH="arm"; CPU="armv7-a"
            EXTRA_FF="--enable-asm --enable-neon"
            EXTRA_CFLAGS="-march=armv7-a -mfloat-abi=softfp -mfpu=neon" ;;
        x86_64)
            TARGET="x86_64-linux-android"
            HOST_TRIPLE="x86_64-linux-android"
            ARCH="x86_64"; CPU="generic"
            EXTRA_FF="--enable-asm --disable-x86asm"
            EXTRA_CFLAGS="" ;;
        *) echo "unknown ABI $ABI"; exit 1 ;;
    esac
    local PREFIX="$ROOT/android/$ABI/prefix"
    local CC="$TC/bin/${TARGET}${API}-clang"
    mkdir -p "$ROOT/android/$ABI"
    echo "=== [$ABI] LAME ==="
    rm -rf "$ROOT/android/$ABI/lame-build"
    cp -r lame-src "$ROOT/android/$ABI/lame-build"
    cd "$ROOT/android/$ABI/lame-build"
    CC="$CC" AR="$TC/bin/llvm-ar" RANLIB="$TC/bin/llvm-ranlib" \
    STRIP="$TC/bin/llvm-strip" \
    CFLAGS="-O2 -fPIC $EXTRA_CFLAGS" \
        ./configure --host="$HOST_TRIPLE" --prefix="$PREFIX" \
        --disable-shared --enable-static --disable-frontend \
        > "$ROOT/logs/android-$ABI-lame.log" 2>&1
    make -j$JOBS >> "$ROOT/logs/android-$ABI-lame.log" 2>&1
    make install >> "$ROOT/logs/android-$ABI-lame.log" 2>&1

    echo "=== [$ABI] FFmpeg ==="
    rm -rf "$ROOT/android/$ABI/ffmpeg-build"
    mkdir -p "$ROOT/android/$ABI/ffmpeg-build"
    cd "$ROOT/android/$ABI/ffmpeg-build"
    local COMMON
    COMMON=$(tr '\n' ' ' < "$ROOT/scripts/ff-config-common.txt")
    # shellcheck disable=SC2086
    "$ROOT/ffmpeg-src/configure" \
        --prefix="$PREFIX" \
        --target-os=android \
        --arch="$ARCH" --cpu="$CPU" \
        --enable-cross-compile \
        --cross-prefix="$TC/bin/llvm-" \
        --cc="$CC" \
        --sysroot="$TC/sysroot" \
        --extra-cflags="-O3 -fPIC $EXTRA_CFLAGS -I$PREFIX/include" \
        --extra-ldflags="-L$PREFIX/lib" \
        $EXTRA_FF \
        $COMMON \
        > "$ROOT/logs/android-$ABI-ffmpeg-configure.log" 2>&1
    make -j$JOBS > "$ROOT/logs/android-$ABI-ffmpeg-make.log" 2>&1
    make install >> "$ROOT/logs/android-$ABI-ffmpeg-make.log" 2>&1

    echo "=== [$ABI] JNI .so ==="
    mkdir -p "$ROOT/android/$ABI/jniLibs"
    "$CC" -O2 -fPIC -shared -Wl,-soname,libvideomp3.so -Wl,-Bsymbolic \
        -I"$ROOT/core" -I"$PREFIX/include" \
        -I"$HOME/tools/jdk17/include" -I"$HOME/tools/jdk17/include/linux" \
        "$ROOT/core/videomp3_core.c" "$ROOT/core/jni_bridge.c" \
        -o "$ROOT/android/$ABI/jniLibs/libvideomp3.so" \
        -L"$PREFIX/lib" -lavformat -lavcodec -lswresample -lavutil -lmp3lame \
        -lm -lz -ldl
    "$TC/bin/llvm-strip" "$ROOT/android/$ABI/jniLibs/libvideomp3.so"
    ls -l "$ROOT/android/$ABI/jniLibs/libvideomp3.so"
}

for ABI in "${ABIS[@]}"; do
    build_abi "$ABI"
done
echo "ANDROID BUILD OK"
