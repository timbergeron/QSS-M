#!/bin/bash
set -euo pipefail

echo "=== setup-vcpkg.sh starting ==="
echo "PWD: $(pwd)"
echo "PATH: $PATH"
echo "USER: $(whoami)"
echo "UID: $(id -u)"

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

# vcpkg ports such as libidn2 require autoconf-archive macros.
if command -v brew >/dev/null 2>&1; then
    if ! brew list --versions autoconf-archive >/dev/null 2>&1; then
        echo "Missing required package: autoconf-archive"
        echo "Install with: brew install autoconf-archive"
        exit 1
    fi
fi

if [ ! -d "vcpkg" ]; then
    git clone --depth 1 https://github.com/microsoft/vcpkg
fi

# If vcpkg dir exists but is incomplete/corrupt, recreate it.
if [ ! -f "./vcpkg/bootstrap-vcpkg.sh" ]; then
    echo "vcpkg directory is incomplete; recreating it"
    rm -rf ./vcpkg
    git clone --depth 1 https://github.com/microsoft/vcpkg
fi

# If the repo exists but the tool binary does not, bootstrap it.
if [ ! -x "./vcpkg/vcpkg" ]; then
    ./vcpkg/bootstrap-vcpkg.sh
fi

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
