#!/bin/bash
# Download + extract the pristine upstream sources the build scripts need.
# Run once from the source package root before build-android.sh.
set -e
cd "$(dirname "$0")/.."
curl -sL -o ffmpeg.tar.xz https://ffmpeg.org/releases/ffmpeg-6.1.2.tar.xz
curl -sL -o lame.tar.gz "https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz"
tar --no-same-owner -xf ffmpeg.tar.xz
tar --no-same-owner -xzf lame.tar.gz
mv ffmpeg-6.1.2 ffmpeg-src
mv lame-3.100 lame-src
echo "SOURCES READY"
