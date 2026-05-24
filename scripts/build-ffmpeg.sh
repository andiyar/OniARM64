#!/usr/bin/env bash
# Build minimal ffmpeg (libavcodec + libavutil) into extern/ffmpeg/.
#
# Only the adpcm_ms decoder is enabled — every other codec / container /
# protocol / encoder / filter is stripped. This produces a tiny pair of
# dylibs (libavcodec + libavutil + their dependency libswresample) with
# no GPL components (x264, x265, libxvid) and no transitive deps (libvpx,
# libdav1d, libmp3lame, libopus, libSvtAv1Enc, openssl).
#
# Why: Homebrew's ffmpeg bottle is built --enable-gpl, which pulls in
# x264/x265 (GPL-2.0-or-later). Bundling those into the redistributable
# .app makes the combined work subject to GPL terms. This script lets
# the bundle ship only LGPL-2.1 components (libavcodec/libavutil), which
# is the actual minimum Oni needs (only ADPCM_MS audio decoding —
# see BFW_SS2_Platform_OpenAL.c:307-411).
#
# See: https://github.com/andiyar/OniARM64/issues/19
#
# Idempotent: skips the build if extern/ffmpeg/lib/libavcodec.dylib
# already exists. Force rebuild with: rm -rf extern/ffmpeg extern/build/ffmpeg-*

set -euo pipefail

FFMPEG_VERSION="8.1.1"
FFMPEG_TARBALL="ffmpeg-${FFMPEG_VERSION}.tar.xz"
FFMPEG_URL="https://ffmpeg.org/releases/${FFMPEG_TARBALL}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXTERN_DIR="$SOURCE_DIR/extern"
DIST_DIR="$EXTERN_DIR/ffmpeg"
BUILD_PARENT="$EXTERN_DIR/build"
BUILD_DIR="$BUILD_PARENT/ffmpeg-${FFMPEG_VERSION}"

# 1. Idempotency check — skip if already built.
if [ -f "$DIST_DIR/lib/libavcodec.dylib" ] && [ -f "$DIST_DIR/lib/libavutil.dylib" ]; then
    echo "build-ffmpeg.sh: $DIST_DIR already has libavcodec + libavutil — skipping."
    echo "(To force rebuild: rm -rf $DIST_DIR $BUILD_DIR)"
    exit 0
fi

mkdir -p "$BUILD_PARENT" "$DIST_DIR"

# 2. Download (cached at $BUILD_PARENT/$FFMPEG_TARBALL on subsequent runs).
if [ ! -f "$BUILD_PARENT/$FFMPEG_TARBALL" ]; then
    echo "build-ffmpeg.sh: downloading $FFMPEG_URL ..."
    curl -fL --progress-bar -o "$BUILD_PARENT/$FFMPEG_TARBALL.tmp" "$FFMPEG_URL"
    mv "$BUILD_PARENT/$FFMPEG_TARBALL.tmp" "$BUILD_PARENT/$FFMPEG_TARBALL"
fi

# 3. Extract.
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
echo "build-ffmpeg.sh: extracting ..."
tar -xJf "$BUILD_PARENT/$FFMPEG_TARBALL" -C "$BUILD_DIR" --strip-components=1

# 4. Configure with the absolute minimum needed for adpcm_ms decode.
#    The pattern is: --disable-everything (turn off all components by
#    default), then --enable-* the specific ones we actually need.
echo "build-ffmpeg.sh: configuring (this prints a lot of output)..."
cd "$BUILD_DIR"
./configure \
    --prefix="$DIST_DIR" \
    --disable-gpl --disable-nonfree \
    --enable-pic --enable-shared --disable-static \
    --disable-doc --disable-programs \
    --disable-everything \
    --enable-decoder=adpcm_ms \
    --disable-network --disable-protocols \
    --disable-devices --disable-filters \
    --disable-muxers --disable-demuxers \
    --disable-parsers --disable-bsfs \
    --disable-encoders \
    --disable-iconv \
    --disable-zlib --disable-lzma --disable-bzlib \
    --disable-avdevice --disable-avfilter --disable-avformat \
    --disable-swresample --disable-swscale \
    --disable-videotoolbox --disable-audiotoolbox \
    --disable-coreimage \
    --disable-xlib --disable-libxcb \
    --disable-libxcb-shm --disable-libxcb-shape --disable-libxcb-xfixes \
    --disable-sdl2 --disable-securetransport

# 5. Build + install.
echo "build-ffmpeg.sh: compiling ..."
make -j"$(sysctl -n hw.ncpu)"
echo "build-ffmpeg.sh: installing into $DIST_DIR ..."
make install

# 6. Report.
echo ""
echo "build-ffmpeg.sh: done. Installed dylibs:"
ls -la "$DIST_DIR/lib/"*.dylib 2>/dev/null | awk '{print "  " $NF " (" $5 " bytes)"}'
echo ""
echo "build-ffmpeg.sh: pkg-config files:"
ls "$DIST_DIR/lib/pkgconfig/"*.pc 2>/dev/null | awk '{print "  " $NF}'
