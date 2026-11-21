#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/host-platform.sh"
glassglide_select_host
glassglide_check_host "$root/.deps"
output="$root/.deps/buildroot-output"
sdk="$root/.deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
sysroot="$output/host/arm-buildroot-linux-gnueabi/sysroot"
toolchain="$output/host/share/buildroot/toolchainfile.cmake"

if [ ! -f "$sysroot/usr/lib/pkgconfig/wpe-webkit-2.0.pc" ]; then
    printf '%s\n' 'Build WPE WebKit with tools/build-wpe.sh first.' >&2
    exit 1
fi

export PKG_CONFIG="$output/host/bin/pkg-config"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"

"$sdk/bin/cmake" -S "$root" -B "$root/build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$sdk/bin/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,relro,-z,now"
"$sdk/bin/cmake" --build "$root/build" --parallel 1
