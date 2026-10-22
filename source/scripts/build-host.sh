#!/bin/bash
# Host (Linux x86_64) build of LAME + trimmed FFmpeg + the test harness.
# The library configuration matches the Android build; the host build only
# adds the ffmpeg CLI + a few generator components so real test media can be
# created (sine/testsrc -> MP4/MKV/AVI with AAC audio).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS=2
# host has no nasm/yasm - borrow the NDK's yasm for FFmpeg's x86 asm checks
export PATH="$HOME/tools/android-ndk-r27c/prebuilt/linux-x86_64/bin:$PATH"
command -v yasm >/dev/null || EXTRA_HOST_FF="--disable-x86asm"

echo "=== host LAME ==="
rm -rf host/lame-build host/prefix
mkdir -p host/prefix
cp -r lame-src host/lame-build
cd host/lame-build
CFLAGS="-O2" ./configure --prefix="$ROOT/host/prefix" \
    --disable-shared --enable-static --disable-frontend >/dev/null
make -j$JOBS >/dev/null
make install >/dev/null
cd "$ROOT"

echo "=== host FFmpeg ==="
mkdir -p host/ffmpeg-build
cd host/ffmpeg-build
COMMON=$(grep -v -e '--disable-programs' -e '--disable-avfilter' \
             -e '--disable-swscale' -e '--disable-avdevice' \
             "$ROOT/scripts/ff-config-common.txt" | tr '\n' ' ')
# shellcheck disable=SC2086
"$ROOT/ffmpeg-src/configure" \
    --prefix="$ROOT/host/prefix" \
    --extra-cflags="-O2 -I$ROOT/host/prefix/include" \
    --extra-ldflags="-L$ROOT/host/prefix/lib" \
    $COMMON \
    $EXTRA_HOST_FF \
    --disable-ffplay --disable-ffprobe \
    --enable-protocol=file,pipe \
    --enable-indev=lavfi \
    --enable-demuxer=lavfi \
    --enable-muxer=mp4,matroska,avi,wav,adts,flac,ogg \
    --enable-encoder=aac,mpeg4,pcm_s16le,flac \
    --enable-decoder=wrapped_avframe,rawvideo \
    --enable-filter=sine,testsrc,anull,anullsrc,aresample,aformat,format,fps,scale,volume \
    > "$ROOT/logs/host-ffmpeg-configure.log" 2>&1
make -j$JOBS > "$ROOT/logs/host-ffmpeg-make.log" 2>&1
make install >> "$ROOT/logs/host-ffmpeg-make.log" 2>&1
cd "$ROOT"

echo "=== host harness ==="
gcc -O2 -Wall -Icore -Ihost/prefix/include \
    core/videomp3_core.c core/test_main.c \
    -o host/harness \
    -Lhost/prefix/lib -lavformat -lavcodec -lswresample -lavutil -lmp3lame \
    -lm -lpthread -lz
echo "HOST BUILD OK"
./host/harness version
