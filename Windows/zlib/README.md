# Bundled Windows zlib

Current Windows builds compile this version from pinned source as a static
library. See [the static build notes](../README-static.md). The DLLs described
below are retained for legacy build configurations.

Both DLLs are zlib 1.3.2 from the MSYS2 MSVCRT packages:

| Architecture | Package | SHA-256 |
| --- | --- | --- |
| x86 | `mingw-w64-i686-zlib-1.3.2-3-any.pkg.tar.zst` | `586f3ec24c0c10e07ddd3bd3280bcf53ee45be7e96a80ee4a2712569c3fe8213` |
| x64 | `mingw-w64-x86_64-zlib-1.3.2-3-any.pkg.tar.zst` | `ffde6f4ed2d879477c93b37ff14c319baf480a2aeafc065edeceafba26b71e90` |

Packages come from `https://repo.msys2.org/mingw/{mingw32,mingw64}/`, with
checksums verified against the repository indexes. These retain the MSVCRT
runtime used by the other bundled libraries. Existing import libraries are
retained after checking their
required symbols against the new DLLs. `zconf.h` uses the packages' offset types
(`z_off_t` is `long`, with separate 64-bit APIs), retaining conditional header
detection so the shared header also works with MSVC.

The x86 1.3.2-3 package imports `libgcc_s_dw2-1.dll`, whose own runtime
requires `libwinpthread-1.dll`. Both are already retained in `Windows/codecs/x86/`;
legacy packaging must include them with zlib and curl. The current static
release packages do not use these DLLs. The package's added minizip header
installation patch does not affect the statically built zlib library.
