# Bundled Windows zlib

Current Windows builds compile this version from pinned source as a static
library. See [the static build notes](../README-static.md). The DLLs described
below are retained for legacy build configurations.

Both DLLs are zlib 1.4 from the MSYS2 MSVCRT packages:

| Architecture | Package | SHA-256 |
| --- | --- | --- |
| x86 | `mingw-w64-i686-zlib-1.4-2-any.pkg.tar.zst` | `d388d6147ef523b06fbea758f130cfed821818749f9364a4bf21554732110f10` |
| x64 | `mingw-w64-x86_64-zlib-1.4-2-any.pkg.tar.zst` | `2a065fb3311d6c08bd2c7431a092b912ade08a83954474e06ea4eb0479d908cd` |

Packages come from `https://repo.msys2.org/mingw/{mingw32,mingw64}/`, with
checksums verified against the repository indexes. These retain the MSVCRT
runtime used by the other bundled libraries. Existing import libraries are
retained after checking their
required symbols against the new DLLs. `zconf.h` uses the packages' offset types
(`z_off_t` is `long`, with separate 64-bit APIs), retaining conditional header
detection so the shared header also works with MSVC.

The x86 1.4-2 package imports `libgcc_s_dw2-1.dll`, whose own runtime
requires `libwinpthread-1.dll`. Both are already retained in `Windows/codecs/x86/`;
legacy packaging must include them with zlib and curl. The current static
release packages do not use these DLLs. The package's added minizip header
installation patch does not affect the statically built zlib library.

Updated for issue #95. The static source archive is zlib 1.4, verified against
the MSYS2 PKGBUILD SHA-256. Both DLLs retain every previous export and their
MSVCRT runtime dependencies. The zlib license is unchanged. Shared headers
include the 1.4 Windows large-file support while retaining portable feature
detection for MSVC.
