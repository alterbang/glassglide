#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/host-platform.sh"
glassglide_select_host
deps="$root/.deps"
downloads="$deps/downloads"
src="$deps/src"
sdk="$deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
glassglide_check_host "$deps"
mkdir -p "$downloads" "$src" "$deps/sdk" "$downloads/buildroot-upstream"
printf '%s\n' "$GLASSGLIDE_HOST_ARCH" > "$deps/.host-arch"

fetch() {
    destination=$1
    url=$2
    expected=$3
    if [ ! -f "$destination" ]; then
        curl --fail --location --retry 2 --silent --show-error \
            --output "$destination.part" "$url"
        printf '%s  %s\n' "$expected" "$destination.part" | sha256sum -c
        mv "$destination.part" "$destination"
    else
        printf '%s  %s\n' "$expected" "$destination" | sha256sum -c
    fi
}

case "$GLASSGLIDE_HOST_ARCH" in
    aarch64)
        sdk_hash=3a9330f3789f8008ece7c8a48f58c3a71d0307a9a1f81896db938b785524c3ce
        clang_packages='
4cb5d48c887e0c1071a058ae8eb30540db77379be50388fcd6b3f9ad1016d54c 10ab922566cfc2fa8a08b0401eb211d839b74d7d clang-19_19.1.7-3+b1_arm64.deb
9c0465b890bc962467043fa47dd0fc4dbb278422c3fdf7f7f0e1c3e6e8a13620 6fb7a082ac9793fdee9413af97f33f5c18a3751f libclang-cpp19_19.1.7-3+b1_arm64.deb
fc0f2c5ffd2b2d7cd0119ed4fb1050c5757d0ed20a4f5c0964d8d737c73b0843 0f0cd56391f65783ef63daf43d43de41fcab4391 libclang-common-19-dev_19.1.7-3+b1_arm64.deb
8a48e1a49f7fcc3be74e697767d72902f4b03041401d1529bcfad1849df39148 6747341239003915b31ca6077d96bed559f73c1b libllvm19_19.1.7-3+b1_arm64.deb'
        ;;
    x86_64)
        sdk_hash=2230a6351e5cce6fcb9d4fdcb4697335f223995401ef969c81744aaeb14c5344
        clang_packages='
bbb74458d782b7792b61b0eed92746df5e6d033778c045cf8c2b3835b7c4efc4 9d79fd4d965b6d51fe4f09f23cacf5be9b1383a4 clang-19_19.1.7-3+b1_amd64.deb
a7d2b6df51050834f6ea5c0d5e34869c2298b0bcdaa91eff17ba3db2cd545c47 436e341b9f66dc5fd1af6fdfa643133bf2938701 libclang-cpp19_19.1.7-3+b1_amd64.deb
4018e40e2d4144fe38c45822e68cca63c1bf6c7872c3073ebb262ad4b2f9b8bc d2a65dae91f68289610efcfd9eb4c4308132cf36 libclang-common-19-dev_19.1.7-3+b1_amd64.deb
db0d614d61345ca710fb73e75105f1ec6e38898fa7b3aa99fa7eb7d8b0a11d57 1815a811f2dcc3c2b27c41f015af2223eb3a5d74 libllvm19_19.1.7-3+b1_amd64.deb'
        ;;
esac
sdk_archive="arm-webos-sdk-2026.08-$GLASSGLIDE_HOST_ARCH.tar.gz"
fetch "$downloads/$sdk_archive" \
    "https://github.com/openlgtv/buildroot-nc4/releases/download/2026.08-webos/arm-webos-linux-gnueabi_sdk-buildroot-$GLASSGLIDE_HOST_ARCH.tar.gz" \
    "$sdk_hash"
fetch "$downloads/wpewebkit-2.54.2.tar.xz" \
    "https://wpewebkit.org/releases/wpewebkit-2.54.2.tar.xz" \
    "f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56"
fetch "$downloads/buildroot-nc4-322ff04.tar.gz" \
    "https://github.com/openlgtv/buildroot-nc4/archive/322ff04e625a7927338c2ac5f3d02cd0511c9b90.tar.gz" \
    "4820e6ee581717d48aeb0cb5259990af264e7335e73bda0a59332cdf7d329105"

# Clang is used only for WebCore's memory-heavy Document.cpp. Keep its
# binaries private; the ARM target still uses the pinned webOS SDK.
while read -r expected snapshot package; do
    [ -n "$expected" ] || continue
    fetch "$downloads/$package" "https://snapshot.debian.org/file/$snapshot" "$expected"
done <<EOF
$clang_packages
EOF

if [ ! -f "$deps/.clang-ready" ]; then
    mkdir -p "$deps/clang19"
    for package in clang-19 libclang-cpp19 libclang-common-19-dev libllvm19; do
        dpkg-deb -x "$downloads/${package}_19.1.7-3+b1_${GLASSGLIDE_DEB_ARCH}.deb" "$deps/clang19"
    done
    if ! LD_LIBRARY_PATH="$deps/clang19/usr/lib/$GLASSGLIDE_HOST_TRIPLET" \
        "$deps/clang19/usr/bin/clang++-19" --version >/dev/null; then
        printf '%s\n' 'Private Clang could not start; check its host library dependencies.' >&2
        exit 1
    fi
    touch "$deps/.clang-ready"
fi

if [ ! -f "$deps/.sdk-ready" ]; then
    tar -xzf "$downloads/$sdk_archive" -C "$deps/sdk"
    "$sdk/relocate-sdk.sh"
    touch "$deps/.sdk-ready"
fi

# Keep all of GCC's built-in specs, including the SDK's linker options, and
# collect the compiler heap more frequently without changing generated code.
gcc_specs="$sdk/lib/gcc/arm-webos-linux-gnueabi/16.2.0/specs"
original_specs="$deps/gcc-sdk-original.specs"
expected_specs="$deps/gcc-low-memory.specs"
if [ ! -f "$original_specs" ]; then
    if [ -e "$gcc_specs" ] || [ -L "$gcc_specs" ]; then
        printf '%s\n' "Cannot record built-in GCC specs while this file exists: $gcc_specs" >&2
        exit 1
    fi
    "$sdk/bin/arm-webos-linux-gnueabi-g++" -dumpspecs > "$original_specs.part"
    mv "$original_specs.part" "$original_specs"
fi
if ! awk -f "$root/tools/wpe_gcc_specs.awk" "$original_specs" > "$expected_specs.part"; then
    rm -f "$expected_specs.part"
    printf '%s\n' 'Cannot prepare low-memory GCC specs from the pinned SDK.' >&2
    exit 1
fi
mv "$expected_specs.part" "$expected_specs"
if [ -e "$gcc_specs" ] || [ -L "$gcc_specs" ]; then
    if [ -L "$gcc_specs" ] || ! cmp -s "$expected_specs" "$gcc_specs"; then
        printf '%s\n' "Unexpected GCC specs in the private SDK: $gcc_specs" >&2
        exit 1
    fi
else
    cp "$expected_specs" "$gcc_specs"
fi

if [ ! -f "$deps/.buildroot-ready" ]; then
    mkdir -p "$src/buildroot"
    tar -xzf "$downloads/buildroot-nc4-322ff04.tar.gz" -C "$src/buildroot" \
        --strip-components=1
    touch "$deps/.buildroot-ready"
fi

# The fork comments out this helper but still calls it, producing `; ;`.
# Restore the upstream check so the external SDK's headers are validated.
helpers="$src/buildroot/toolchain/helpers.mk"
if grep -q '^#check_kernel_headers_version = ' "$helpers"; then
    sed -i '/^#check_kernel_headers_version = /,/^#[[:space:]]*fi$/s/^#//' "$helpers"
fi
if ! grep -q '^check_kernel_headers_version = ' "$helpers"; then
    printf '%s\n' 'Buildroot kernel header check could not be restored.' >&2
    exit 1
fi

upstream="https://raw.githubusercontent.com/buildroot/buildroot/d5180309b1b66ef3b8eaccca70ad69be8e0729a1"
while read -r expected relative; do
    destination="$downloads/buildroot-upstream/$relative"
    mkdir -p "$(dirname "$destination")"
    fetch "$destination" "$upstream/$relative" "$expected"
    cp "$destination" "$src/buildroot/$relative"
done <<'EOF'
d0f2b88753833162be6982397513311a5413fbe97374fee509119b74e274e5cb package/wayland/wayland.mk
f6a74eaa7ce10d22e638c6baa79bd1d1f77f812c122599a741890dbdc7de0bf4 package/wayland/wayland.hash
461bf71368bc964933a5fd375b68320d3a42a0a8164aac1a4e0a06219a6c78bb package/wayland-protocols/wayland-protocols.mk
85951ffc6fcff14889752f3548e2d773a73f1dd324e9889c649b893072ff69ca package/wayland-protocols/wayland-protocols.hash
EOF

# The local fixture uses WPE's modern platform, not the legacy FDO backend.
buildroot_patch="$root/buildroot/patches/buildroot/0001-wpewebkit-modern-platform.patch"
if patch --silent --force --reverse --dry-run -d "$src/buildroot" -p1 < "$buildroot_patch" >/dev/null 2>&1; then
    :
elif patch --silent --force --forward --dry-run -d "$src/buildroot" -p1 < "$buildroot_patch" >/dev/null 2>&1; then
    patch --silent --force --forward -d "$src/buildroot" -p1 < "$buildroot_patch"
else
    printf '%s\n' 'Buildroot WPE dependency patch does not match the pinned source.' >&2
    exit 1
fi

wpe_hash="$src/buildroot/package/wpewebkit/wpewebkit.hash"
wpe_entry='sha256  f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56  wpewebkit-2.54.2.tar.xz'
if ! grep -Fxq "$wpe_entry" "$wpe_hash"; then
    printf '\n# WPE 2.54.2 source verified by this project\n%s\n' "$wpe_entry" >> "$wpe_hash"
fi

mkdir -p "$deps/buildroot-downloads/wpewebkit"
cp "$downloads/wpewebkit-2.54.2.tar.xz" \
    "$deps/buildroot-downloads/wpewebkit/wpewebkit-2.54.2.tar.xz"
printf 'Dependencies prepared in %s\n' "$deps"
