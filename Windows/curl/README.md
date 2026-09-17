# Bundled Windows libcurl

Both architectures use curl 8.22.0, built from the unmodified upstream release:

- Source: https://curl.se/download/curl-8.22.0.tar.xz
- SHA-256: `f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7`

The source hash was checked against MSYS2's curl 8.22.0-1 recipe. The dependency
watch uses that package's version as the upstream notification baseline; these
DLLs are built locally rather than copied from the MSYS2 curl package.

Schannel supplies TLS and the Windows certificate store. The only non-system
runtime dependency is the bundled `zlib1.dll`. OpenSSL, libssh2, HTTP/2, Brotli,
Zstandard and libpsl are disabled to preserve the standalone Windows package.
Windows IDN support imports the system `Normaliz.dll`. All exports from the
previous DLLs remain available, so the existing `libcurl.lib` import libraries
are retained. Public headers come from the same source release.

## Rebuilding

Extract the source outside the repository and run the following from the
repository root with CMake and the MinGW-w64 cross toolchains installed. Set
`curl_source` to the extracted directory and `curl_build` to a scratch directory.
The September 2026 binaries were built with MinGW-w64 GCC 13 (win32 threads).

```sh
for arch in x86 x64; do
  case "$arch" in
    x86) target=i686-w64-mingw32 ;;
    x64) target=x86_64-w64-mingw32 ;;
  esac
  cmake -S "$curl_source" -B "$curl_build/$arch" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$target-gcc" \
    -DCMAKE_RC_COMPILER="$target-windres" \
    -DCMAKE_BUILD_TYPE=Release \
    '-DCMAKE_SHARED_LINKER_FLAGS=-static-libgcc -Wl,--no-insert-timestamp' \
    -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF \
    -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
    -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_WINDOWS_SSPI=ON \
    -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF \
    -DUSE_LIBIDN2=OFF -DUSE_WIN32_IDN=ON -DUSE_NGHTTP2=OFF \
    -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_USE_PKGCONFIG=OFF \
    -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON \
    -DCURL_ENABLE_NTLM=ON -DCURL_ENABLE_SMB=ON \
    -DCURL_ZLIB=ON -DZLIB_INCLUDE_DIR="$PWD/Windows/zlib/include" \
    -DZLIB_LIBRARY="$PWD/Windows/zlib/$arch/zlib1.lib"
  cmake --build "$curl_build/$arch" --parallel 4
  "$target-strip" --strip-unneeded "$curl_build/$arch/lib/libcurl.dll"
done
```

Before copying the DLLs into `lib/x86/` and `lib/x64/`, compare their exports with
the existing DLLs and check every PE import. Keep the public headers in sync and
update `.github/dependency-watch.json` only after replacing the actual binaries.
