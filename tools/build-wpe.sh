#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/host-platform.sh"
glassglide_select_host
deps="$root/.deps"
glassglide_check_host "$deps"
source_dir="$deps/src/buildroot"
output="$deps/buildroot-output"
sdk="$deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
target=${1:-wpewebkit}
jobs=${GLASSGLIDE_BUILD_JOBS:-1}

if [ "$#" -gt 1 ] || { [ "$target" != wpewebkit ] && [ "$target" != wpewebkit-rebuild ] \
        && [ "$target" != legal-info ] && [ "$target" != libgcrypt-reconfigure ]; }; then
    printf '%s\n' 'Usage: tools/build-wpe.sh [wpewebkit|wpewebkit-rebuild|legal-info|libgcrypt-reconfigure]' >&2
    exit 1
fi
case "$jobs" in
    1|2) ;;
    *) printf '%s\n' 'GLASSGLIDE_BUILD_JOBS must be 1 or 2.' >&2; exit 1 ;;
esac

if [ ! -f "$deps/.sdk-ready" ] || [ ! -f "$deps/.buildroot-ready" ] \
        || [ ! -f "$deps/.clang-ready" ]; then
    printf '%s\n' 'Run tools/prepare-deps.sh first.' >&2
    exit 1
fi
gcc_specs="$sdk/lib/gcc/arm-webos-linux-gnueabi/16.2.0/specs"
if [ -L "$gcc_specs" ] || ! cmp -s "$deps/gcc-low-memory.specs" "$gcc_specs"; then
    printf '%s\n' 'Private SDK GCC specs are missing or changed; run tools/prepare-deps.sh.' >&2
    exit 1
fi

mkdir -p "$output"
export BR2_DL_DIR="$deps/buildroot-downloads"
export WK_USE_CCACHE=NO
make -C "$source_dir" O="$output" \
    BR2_DEFCONFIG="$root/buildroot/glassglide_defconfig" defconfig

# Local-fixture prototype only. This build cannot safely browse the web:
# webOS 3.9's kernel lacks the standard WPE bubblewrap sandbox facilities.
options="-DPORT=WPE -DENABLE_WPE_PLATFORM=ON -DENABLE_WPE_PLATFORM_WAYLAND=ON \
-DENABLE_WPE_PLATFORM_DRM=OFF -DENABLE_WPE_PLATFORM_HEADLESS=OFF \
-DENABLE_WPE_LEGACY_API=OFF -DENABLE_WPE_1_1_API=OFF \
-DDEVELOPER_MODE=OFF -DENABLE_REMOTE_INSPECTOR=OFF -DENABLE_BUBBLEWRAP_SANDBOX=OFF \
-DENABLE_VIDEO=OFF -DENABLE_WEB_AUDIO=OFF -DENABLE_WEB_CODECS=OFF \
-DENABLE_MEDIA_STREAM=OFF -DENABLE_WEB_RTC=OFF -DENABLE_GAMEPAD=OFF \
-DENABLE_SPEECH_SYNTHESIS=OFF -DENABLE_JOURNALD_LOG=OFF \
-DENABLE_SPELLCHECK=OFF -DENABLE_PDFJS=OFF -DENABLE_WEBGL=OFF \
-DENABLE_WEBDRIVER=OFF -DENABLE_API_TESTS=OFF -DENABLE_MINIBROWSER=OFF \
-DENABLE_DOCUMENTATION=OFF -DENABLE_INTROSPECTION=OFF -DUSE_ATK=OFF \
-DUSE_GBM=OFF -DUSE_LIBDRM=OFF -DUSE_GSTREAMER=OFF -DUSE_SKIA=ON \
-DUSE_AVIF=OFF -DUSE_LCMS=OFF -DUSE_JPEGXL=OFF -DUSE_WOFF2=OFF \
-DUSE_LIBBACKTRACE=OFF -DUSE_LIBHYPHEN=OFF -DUSE_SYSPROF_CAPTURE=OFF \
-DCMAKE_SHARED_LINKER_FLAGS=-tno-glibc-polyfill \
-DGLASSGLIDE_DOCUMENT_LAUNCHER=$root/tools/wpe_document_compiler.py"

make -C "$source_dir" O="$output" \
    BR2_TOOLCHAIN_EXTERNAL_PATH="$sdk" \
    BR2_CMAKE="$sdk/bin/cmake" \
    PARALLEL_JOBS="$jobs" \
    NINJA_OPTS="-j$jobs" \
    BR2_GLOBAL_PATCH_DIR="$root/buildroot/patches" \
    LIBGCRYPT_CONF_OPTS="--disable-tests --disable-asm --disable-arm-crypto-support --with-gpg-error-prefix=$output/host/arm-buildroot-linux-gnueabi/sysroot/usr" \
    WAYLAND_CONF_OPTS="-Dtests=false -Ddocumentation=false -Degl=false" \
    WPEWEBKIT_VERSION=2.54.2 \
    WPEWEBKIT_SUPPORTS_IN_SOURCE_BUILD=NO \
    WPEWEBKIT_CONF_OPTS="$options" \
    "$target"
