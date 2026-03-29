#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(
  CDPATH= cd -- "$(dirname -- "$0")" && pwd
)"

VCPKG_REPO_URL="${VCPKG_REPO_URL:-https://github.com/microsoft/vcpkg}"
# Pin vcpkg so CI and local builds do not float with upstream master.
VCPKG_COMMIT="${VCPKG_COMMIT:-c27eeddba73f608f10605d80bc0144c1166f8fb7}"

echo "=== setup-vcpkg.sh starting ==="
echo "PWD: $(pwd)"
echo "PATH: $PATH"
echo "USER: $(whoami)"
echo "UID: $(id -u)"
echo "VCPKG_REPO_URL: $VCPKG_REPO_URL"
echo "VCPKG_COMMIT: $VCPKG_COMMIT"

if [ "$(id -u)" -eq 0 ]; then
    echo "Do not run this script with sudo."
    echo "It creates root-owned files and can hide Homebrew tools from PATH."
    echo "Run as your normal user."
    exit 1
fi

echo ""
echo "=== Checking required commands ==="
required_cmds=(git lipo autoconf automake pkg-config)
missing=()
for cmd in "${required_cmds[@]}"; do
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "  $cmd: OK ($(command -v $cmd))"
    else
        echo "  $cmd: NOT FOUND"
        missing+=("$cmd")
    fi
done

# vcpkg autotools ports often expect "libtoolize". Homebrew installs
# GNU libtool as "glibtoolize" on macOS to avoid clashing with Apple libtool.
echo ""
echo "=== Checking libtoolize ==="
if ! command -v libtoolize >/dev/null 2>&1; then
    echo "  libtoolize not found, checking for glibtoolize..."
    if command -v glibtoolize >/dev/null 2>&1; then
        echo "  glibtoolize found at $(command -v glibtoolize), creating shim..."
        shim_dir="$(pwd)/.tool-shims"
        mkdir -p "$shim_dir"
        cat > "$shim_dir/libtoolize" <<'EOF'
#!/bin/sh
exec glibtoolize "$@"
EOF
        chmod +x "$shim_dir/libtoolize"
        export PATH="$shim_dir:$PATH"
        echo "  Shim created at $shim_dir/libtoolize"
        echo "  PATH updated: $PATH"
    else
        echo "  glibtoolize also NOT FOUND"
        missing+=("libtool/libtoolize")
    fi
else
    echo "  libtoolize: OK ($(command -v libtoolize))"
fi

if [ "${#missing[@]}" -gt 0 ]; then
    echo ""
    echo "Missing required tools: ${missing[*]}"
    echo "Install with: brew install autoconf automake libtool pkg-config autoconf-archive"
    exit 1
fi

echo ""
echo "=== All required tools found ==="

if [ -n "${VCPKG_DEFAULT_BINARY_CACHE:-}" ]; then
    mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
    echo "Using VCPKG_DEFAULT_BINARY_CACHE: $VCPKG_DEFAULT_BINARY_CACHE"
fi

if [ -n "${VCPKG_DOWNLOADS:-}" ]; then
    mkdir -p "$VCPKG_DOWNLOADS"
    echo "Using VCPKG_DOWNLOADS: $VCPKG_DOWNLOADS"
fi

# vcpkg ports such as libidn2 require autoconf-archive macros.
if command -v brew >/dev/null 2>&1; then
    if ! brew list --versions autoconf-archive >/dev/null 2>&1; then
        echo "Missing required package: autoconf-archive"
        echo "Install with: brew install autoconf-archive"
        exit 1
    fi
fi

ensure_pinned_vcpkg_checkout() {
    rm -rf ./vcpkg
    mkdir -p ./vcpkg
    git -C ./vcpkg init -q
    git -C ./vcpkg remote add origin "$VCPKG_REPO_URL"
    git -C ./vcpkg fetch --depth 1 origin "$VCPKG_COMMIT"
    git -C ./vcpkg checkout --force --detach FETCH_HEAD
}

apply_local_vcpkg_patches() {
    local nettle_port_dir="./vcpkg/ports/nettle"
    local nettle_patch_src="$SCRIPT_DIR/vcpkg-patches/nettle-gnu23-prototypes.patch"
    local nettle_patch_dst="$nettle_port_dir/qssm-macos-gnu23-prototypes.patch"
    local nettle_portfile="$nettle_port_dir/portfile.cmake"
    local tmp_portfile

    cp "$nettle_patch_src" "$nettle_patch_dst"

    if ! grep -q 'qssm-macos-gnu23-prototypes.patch' "$nettle_portfile"; then
        tmp_portfile="$(mktemp "${TMPDIR:-/tmp}/qssm-nettle-portfile.XXXXXX")"
        awk '
            { print }
            !done && /msvc-support\.patch/ {
                print "        qssm-macos-gnu23-prototypes.patch"
                done = 1
            }
        ' "$nettle_portfile" > "$tmp_portfile"
        mv "$tmp_portfile" "$nettle_portfile"
    fi

    if ! grep -q 'qssm-macos-gnu23-prototypes.patch' "$nettle_portfile"; then
        echo "Failed to inject local nettle patch into vcpkg portfile"
        exit 1
    fi

    echo "Applied local nettle port patch: qssm-macos-gnu23-prototypes.patch"
}

if [ ! -d "./vcpkg/.git" ] || [ ! -f "./vcpkg/bootstrap-vcpkg.sh" ]; then
    echo "vcpkg checkout missing or incomplete; creating pinned checkout"
    ensure_pinned_vcpkg_checkout
fi

current_vcpkg_commit="$(git -C ./vcpkg rev-parse HEAD 2>/dev/null || true)"
if [ "$current_vcpkg_commit" != "$VCPKG_COMMIT" ]; then
    echo "vcpkg checkout is at ${current_vcpkg_commit:-<unknown>}; resetting to pinned commit $VCPKG_COMMIT"
    ensure_pinned_vcpkg_checkout
    current_vcpkg_commit="$(git -C ./vcpkg rev-parse HEAD 2>/dev/null || true)"
fi

apply_local_vcpkg_patches

# If the repo exists but the tool binary does not, bootstrap it.
if [ ! -x "./vcpkg/vcpkg" ]; then
    ./vcpkg/bootstrap-vcpkg.sh
fi

echo "Using vcpkg checkout: $current_vcpkg_commit"

./vcpkg/vcpkg install --overlay-triplets=custom-triplets --triplet=x64-osx-1013 zlib libogg opus opusfile libvorbis libmad libflac libxmp libgnutls
./vcpkg/vcpkg install --overlay-triplets=custom-triplets --triplet=arm64-osx-11 zlib libogg opus opusfile libvorbis libmad libflac libxmp libgnutls

mkdir -p libs_universal
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libogg.a ./vcpkg/installed/arm64-osx-11/lib/libogg.a -output ./libs_universal/libogg.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libopus.a ./vcpkg/installed/arm64-osx-11/lib/libopus.a -output ./libs_universal/libopus.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libopusfile.a ./vcpkg/installed/arm64-osx-11/lib/libopusfile.a -output ./libs_universal/libopusfile.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libvorbis.a ./vcpkg/installed/arm64-osx-11/lib/libvorbis.a -output ./libs_universal/libvorbis.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libvorbisenc.a ./vcpkg/installed/arm64-osx-11/lib/libvorbisenc.a -output ./libs_universal/libvorbisenc.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libvorbisfile.a ./vcpkg/installed/arm64-osx-11/lib/libvorbisfile.a -output ./libs_universal/libvorbisfile.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libz.a ./vcpkg/installed/arm64-osx-11/lib/libz.a -output ./libs_universal/libz.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libmad.a ./vcpkg/installed/arm64-osx-11/lib/libmad.a -output ./libs_universal/libmad.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libFLAC.a ./vcpkg/installed/arm64-osx-11/lib/libFLAC.a -output ./libs_universal/libFLAC.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libxmp.a ./vcpkg/installed/arm64-osx-11/lib/libxmp.a -output ./libs_universal/libxmp.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libgnutls.a ./vcpkg/installed/arm64-osx-11/lib/libgnutls.a -output ./libs_universal/libgnutls.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libnettle.a ./vcpkg/installed/arm64-osx-11/lib/libnettle.a -output ./libs_universal/libnettle.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libhogweed.a ./vcpkg/installed/arm64-osx-11/lib/libhogweed.a -output ./libs_universal/libhogweed.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libgmp.a ./vcpkg/installed/arm64-osx-11/lib/libgmp.a -output ./libs_universal/libgmp.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libidn2.a ./vcpkg/installed/arm64-osx-11/lib/libidn2.a -output ./libs_universal/libidn2.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libunistring.a ./vcpkg/installed/arm64-osx-11/lib/libunistring.a -output ./libs_universal/libunistring.a
lipo -create ./vcpkg/installed/x64-osx-1013/lib/libtasn1.a ./vcpkg/installed/arm64-osx-11/lib/libtasn1.a -output ./libs_universal/libtasn1.a
# libiconv is provided by macOS natively; vcpkg emits an empty package, so no .a to lipo
