#!/bin/sh

glassglide_select_host() {
    case "$(uname -s):$(uname -m)" in
        Linux:aarch64|Linux:arm64)
            GLASSGLIDE_HOST_ARCH=aarch64
            GLASSGLIDE_DEB_ARCH=arm64
            GLASSGLIDE_HOST_TRIPLET=aarch64-linux-gnu
            GLASSGLIDE_ELF_MACHINE=AArch64
            ;;
        Linux:x86_64|Linux:amd64)
            GLASSGLIDE_HOST_ARCH=x86_64
            GLASSGLIDE_DEB_ARCH=amd64
            GLASSGLIDE_HOST_TRIPLET=x86_64-linux-gnu
            GLASSGLIDE_ELF_MACHINE='Advanced Micro Devices X86-64'
            ;;
        *)
            printf '%s\n' 'Unsupported host: use Linux ARM64 or x86_64.' >&2
            return 1
            ;;
    esac
}

glassglide_check_host() {
    deps=$1
    marker="$deps/.host-arch"
    if [ -f "$marker" ] && [ "$(cat "$marker")" != "$GLASSGLIDE_HOST_ARCH" ]; then
        printf '%s\n' "Build directory belongs to another host architecture: $deps" >&2
        return 1
    fi
    for binary in \
        "$deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot/bin/cmake" \
        "$deps/clang19/usr/bin/clang++-19" \
        "$deps/buildroot-output/host/bin/ninja"; do
        if [ -f "$binary" ] && ! LC_ALL=C readelf -h "$binary" \
                | grep -Eq "Machine:[[:space:]]+$GLASSGLIDE_ELF_MACHINE$"; then
            printf '%s\n' "Incompatible host binary: $binary" >&2
            return 1
        fi
    done
}
