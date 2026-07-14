#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(
    CDPATH= cd -- "$(dirname -- "$0")" && pwd
)"
cd "$SCRIPT_DIR"

VCPKG_REPO_URL="${VCPKG_REPO_URL:-https://github.com/microsoft/vcpkg}"
# Pin vcpkg so CI and local builds do not float with upstream master.
VCPKG_COMMIT="${VCPKG_COMMIT:-c27eeddba73f608f10605d80bc0144c1166f8fb7}"
GNUTLS_VERSION="3.8.13"
GNUTLS_SHA512="71bf189a836fd18d58b9e995d4bfcecdb0aae6129dfd44247b98422b2f127dd868f9905d28fad2ca05afd919a0e6b3c8eebb6b95804067d3a8dab31ebdc72453"
LIBTASN1_VERSION="4.21.0"
LIBTASN1_SHA512="6a581c4c072b168bf29a0dec7e59a9329a798e392b7d1033791d0e3166a5d1164e2a7065373a84018d500a01563657900c318b1fd437c227c3174b754f9998d3"
NETTLE_VERSION="3.10.2"
NETTLE_REF="nettle_3.10.2_release_20250626"
NETTLE_SHA512="adb6f0ba6b4b7c64a27dc554a6cbdf9cbdbbacaedbf535daed3ac54cb2bebab503ecac4d8890d17469d510042c35e66c4c4aa9fd56a08e01c20db1e0bdb2bd07"

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
required_cmds=(git lipo autoconf automake pkg-config perl)
missing=()
for cmd in "${required_cmds[@]}"; do
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "  $cmd: OK ($(command -v "$cmd"))"
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
        shim_dir="$(mktemp -d "${TMPDIR:-/tmp}/qssm-tool-shims.XXXXXX")"
        trap 'rm -rf "$shim_dir"' EXIT
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

replace_port_value() {
    local file="$1"
    local old="$2"
    local new="$3"

    if grep -Fq "$old" "$file"; then
        OLD_VALUE="$old" NEW_VALUE="$new" perl -0pi -e \
            's/\Q$ENV{OLD_VALUE}\E/$ENV{NEW_VALUE}/g' "$file"
    elif ! grep -Fq "$new" "$file"; then
        echo "Expected value not found in $file: $old"
        exit 1
    fi
}

apply_crypto_port_overrides() {
    local gnutls_port="./vcpkg/ports/libgnutls"
    local libtasn1_port="./vcpkg/ports/libtasn1"
    local nettle_port="./vcpkg/ports/nettle"

    # Remove the compatibility patch used by Nettle 3.10. The fixes are
    # upstream in 3.10.2, and this also migrates existing local checkouts.
    replace_port_value "$nettle_port/portfile.cmake" \
        '        qssm-macos-gnu23-prototypes.patch' ''
    rm -f "$nettle_port/qssm-macos-gnu23-prototypes.patch"

    replace_port_value "$gnutls_port/vcpkg.json" \
        '"version": "3.8.12"' "\"version\": \"$GNUTLS_VERSION\""
    replace_port_value "$gnutls_port/portfile.cmake" \
        '332a8e5200461517c7f08515e3aaab0bec6222747422e33e9e7d25d35613e3d0695a803fce226bd6a83f723054f551328bd99dcf0573e142be777dcf358e1a3b' \
        "$GNUTLS_SHA512"

    replace_port_value "$libtasn1_port/vcpkg.json" \
        '"version": "4.19.0"' "\"version\": \"$LIBTASN1_VERSION\""
    replace_port_value "$libtasn1_port/vcpkg.json" \
        '  "port-version": 3,' ''
    # libtasn1 4.21.0 includes the gnulib fortify guards carried by this
    # older vcpkg patch, so applying it again fails as already integrated.
    replace_port_value "$libtasn1_port/portfile.cmake" \
        '        clang-fortify.patch # ported from https://git.savannah.gnu.org/cgit/gnulib.git/commit/?id=522aea1093a598246346b3e1c426505c344fe19a' ''
    replace_port_value "$libtasn1_port/portfile.cmake" \
        '        "${SOURCE_PATH}/doc/COPYING.LESSER"' \
        '        "${SOURCE_PATH}/COPYING.LESSERv2"'
    replace_port_value "$libtasn1_port/portfile.cmake" \
        '        "${SOURCE_PATH}/doc/COPYING"' ''
    replace_port_value "$libtasn1_port/portfile.cmake" \
        '287f5eddfb5e21762d9f14d11997e56b953b980b2b03a97ed4cd6d37909bda1ed7d2cdff9da5d270a21d863ab7e54be6b85c05f1075ac5d8f0198997cf335ef4' \
        "$LIBTASN1_SHA512"

    replace_port_value "$nettle_port/vcpkg.json" \
        '"version": "3.10"' "\"version\": \"$NETTLE_VERSION\""
    replace_port_value "$nettle_port/vcpkg.json" \
        '  "port-version": 1,' ''
    replace_port_value "$nettle_port/portfile.cmake" \
        'nettle_3.10_release_20240616' "$NETTLE_REF"
    replace_port_value "$nettle_port/portfile.cmake" \
        '8767e4f0c34ce76ead5d66f06f97e6b184d439fa94f848ee440196fafde3da2ea7cfc54f9bd8f9ab6a99929b0d14b3d5a28857e05d954551e94b619598c17659' \
        "$NETTLE_SHA512"

    echo "Using macOS crypto ports: GnuTLS $GNUTLS_VERSION, libtasn1 $LIBTASN1_VERSION, Nettle $NETTLE_VERSION"
}

remove_stale_crypto_packages() {
    local spec
    local installed_version
    local expected_version
    local -a stale_specs=()

    while read -r spec installed_version _; do
        case "${spec%%:*}" in
            libgnutls) expected_version="$GNUTLS_VERSION" ;;
            libtasn1) expected_version="$LIBTASN1_VERSION" ;;
            nettle) expected_version="$NETTLE_VERSION" ;;
            *) continue ;;
        esac

        installed_version="${installed_version%%#*}"
        if [ "$installed_version" != "$expected_version" ]; then
            stale_specs+=("$spec")
        fi
    done < <(./vcpkg/vcpkg list)

    if [ "${#stale_specs[@]}" -gt 0 ]; then
        echo "Removing stale crypto packages: ${stale_specs[*]}"
        ./vcpkg/vcpkg remove --recurse "${stale_specs[@]}"
    fi
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

apply_crypto_port_overrides

# If the repo exists but the tool binary does not, bootstrap it.
if [ ! -x "./vcpkg/vcpkg" ]; then
    ./vcpkg/bootstrap-vcpkg.sh
fi

echo "Using vcpkg checkout: $current_vcpkg_commit"
remove_stale_crypto_packages

# git.lysator.liu.se (nettle's canonical host) suffers periodic outages that
# fail the build at the nettle source download. The GitHub mirror gnutls/nettle
# publishes byte-identical archives of the same tag (same SHA512 the port pins),
# so pre-seed vcpkg's download cache from there. Best-effort: on any problem we
# leave the cache untouched and let vcpkg fall back to its default source.
prefetch_nettle_source() {
    # CI overrides the download dir via VCPKG_DOWNLOADS; honor it so the seed
    # lands where vcpkg actually looks (defaults to the in-tree vcpkg/downloads).
    local downloads="${VCPKG_DOWNLOADS:-./vcpkg/downloads}"
    local filename="nettle-nettle-${NETTLE_REF}.tar.gz"
    local dest="${downloads}/${filename}"
    local url="https://github.com/gnutls/nettle/archive/refs/tags/${NETTLE_REF}.tar.gz"

    if [ -f "$dest" ] && [ "$(shasum -a 512 "$dest" | awk '{print $1}')" = "$NETTLE_SHA512" ]; then
        echo "Nettle source already cached (SHA512 OK); skipping mirror prefetch."
        return 0
    fi

    mkdir -p "$downloads"
    local tmp="${downloads}/.nettle-mirror.$$.tar.gz"
    echo "Prefetching nettle source from GitHub mirror (git.lysator.liu.se fallback)..."
    if curl -fsSL --retry 3 --max-time 180 -o "$tmp" "$url" \
        && [ "$(shasum -a 512 "$tmp" | awk '{print $1}')" = "$NETTLE_SHA512" ]; then
        mv -f "$tmp" "$dest"
        echo "  cached $filename from GitHub mirror (SHA512 verified)."
    else
        rm -f "$tmp"
        echo "  GitHub mirror unavailable or hash mismatch; using vcpkg default source."
    fi
    return 0
}
prefetch_nettle_source || true

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
