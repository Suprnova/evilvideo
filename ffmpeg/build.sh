#!/usr/bin/env bash
# Builds the trimmed, static, LGPL ffmpeg.exe that ships with evilvideo.
#
# Usage: ffmpeg/build.sh [output folder]   (default: build/ffmpeg)
#
# Needs curl, tar, xz, make, nasm, pkg-config, meson, ninja and the MinGW-w64 cross GCC
# (x86_64-w64-mingw32-gcc). Writes to the output folder everything that distributing the
# binary requires:
#   ffmpeg.exe
#   licenses/   the LGPL 3 and GPL 3 that ffmpeg is distributed under, dav1d's BSD license,
#               and ffmpeg-build.txt (versions, where to find the exact sources and this
#               script at the current commit, and the configure line)
set -euo pipefail

ffmpeg_version=9.0.2
ffmpeg_sha256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
ffmpeg_url=https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz
ffmpeg_repo=https://github.com/FFmpeg/FFmpeg/tree/n$ffmpeg_version

dav1d_version=1.5.4
dav1d_sha256=686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5
dav1d_url=https://downloads.videolan.org/pub/videolan/dav1d/$dav1d_version/dav1d-$dav1d_version.tar.xz

host=x86_64-w64-mingw32

configure_flags=(
    --target-os=mingw32 --arch=x86_64 --cross-prefix=$host- --enable-cross-compile
    --pkg-config=pkg-config --pkg-config-flags=--static --extra-ldflags=-static
    --disable-everything --disable-autodetect --disable-network --disable-doc --disable-debug
    --disable-ffplay --disable-ffprobe --enable-small
    --enable-libdav1d
    --enable-protocol=file,pipe
    --enable-demuxer=mov,matroska,avi,mpegts,mpegps,mpegvideo,flv,ogg,asf,wav,mp3,aac,flac
    --enable-decoder=h264,hevc,vp8,vp9,libdav1d,mpeg1video,mpeg2video,mpeg4,msmpeg4v3,wmv1,wmv2,wmv3,vc1,mjpeg,prores,theora
    --enable-decoder=aac,mp3,mp3float,mp2,ac3,eac3,opus,vorbis,flac,alac,wmav2,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8
    --enable-parser=h264,hevc,vp8,vp9,av1,mpegvideo,mpeg4video,vc1,mjpeg,aac,mpegaudio,ac3,opus,vorbis,flac
    --enable-filter=buffer,buffersink,abuffer,abuffersink,null,anull,fps,scale,pad,setsar,format,aformat,aresample
    --enable-encoder=mjpeg,pcm_s16le
    --enable-muxer=image2,wav
)

revision=$(git -C "$(dirname "$0")" rev-parse HEAD 2>/dev/null || echo main)
script_url=https://github.com/Suprnova/evilvideo/blob/$revision/ffmpeg/build.sh

out=$(realpath -m "${1:-build/ffmpeg}")
work=$out/work
prefix=$work/prefix
jobs=$(nproc)

fetch() { # url sha256
    local file=$work/${1##*/}
    curl -fsSL -o "$file" "$1"
    echo "$2  $file" | sha256sum -c --quiet
    tar -xJf "$file" -C "$work"
}

rm -rf "$work"
mkdir -p "$work"
fetch "$dav1d_url" "$dav1d_sha256"
fetch "$ffmpeg_url" "$ffmpeg_sha256"

cat > "$work/cross.ini" <<EOF
[binaries]
c = '$host-gcc'
ar = '$host-ar'
strip = '$host-strip'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

meson setup "$work/dav1d-build" "$work/dav1d-$dav1d_version" --cross-file "$work/cross.ini" \
    --prefix "$prefix" --libdir lib --buildtype release --default-library static \
    -Denable_tools=false -Denable_tests=false -Denable_examples=false
ninja -C "$work/dav1d-build" install

cd "$work/ffmpeg-$ffmpeg_version"
PKG_CONFIG_LIBDIR=$prefix/lib/pkgconfig ./configure "${configure_flags[@]}"
make -j"$jobs" ffmpeg.exe

rm -rf "$out/licenses"
mkdir -p "$out/licenses"
cp ffmpeg.exe "$out/"
cp COPYING.LGPLv3 "$out/licenses/ffmpeg-LGPL-3.0.txt"
cp COPYING.GPLv3 "$out/licenses/ffmpeg-GPL-3.0.txt"
cp "$work/dav1d-$dav1d_version/COPYING" "$out/licenses/dav1d-BSD-2-Clause.txt"
cat > "$out/licenses/ffmpeg-build.txt" <<EOF
ffmpeg.exe is FFmpeg $ffmpeg_version, statically linked with dav1d $dav1d_version. Neither is
modified.

FFmpeg is licensed under the GNU LGPL version 2.1 or (at your option) any later version.
This copy is distributed under the GNU LGPL version 3 (ffmpeg-LGPL-3.0.txt), which
supplements the GNU GPL version 3 (ffmpeg-GPL-3.0.txt). dav1d is licensed under the BSD
2-Clause license (dav1d-BSD-2-Clause.txt).

Corresponding source: FFmpeg's source at the tag below, built by the build script below
with this configure line.

FFmpeg $ffmpeg_version
Source: $ffmpeg_repo
        $ffmpeg_url
Configure: ${configure_flags[*]}

dav1d $dav1d_version
Source: $dav1d_url

Build script: $script_url
EOF
rm -rf "$work"
