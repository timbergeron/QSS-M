# Bundled Windows zlib

Both DLLs are zlib 1.3.2 from the MSYS2 MSVCRT packages:

| Architecture | Package | SHA-256 |
| --- | --- | --- |
| x86 | `mingw-w64-i686-zlib-1.3.2-2-any.pkg.tar.zst` | `03d1f7030adc892eb1ee8434f7201847093ff69b8ad9ff77a650d6dfae72083a` |
| x64 | `mingw-w64-x86_64-zlib-1.3.2-2-any.pkg.tar.zst` | `9e75842a070ba648e986e12424e1c92c9d7d77200e85f6a34eeb600819f2e694` |

Packages come from `https://repo.msys2.org/mingw/{mingw32,mingw64}/`, with
checksums verified against the repository indexes. These replace the previous
UCRT builds of the same version so zlib uses the same C runtime as the other
bundled libraries. Existing import libraries are retained after checking their
required symbols against the new DLLs. `zconf.h` uses the packages' offset types
(`z_off_t` is `long`, with separate 64-bit APIs), retaining conditional header
detection so the shared header also works with MSVC.
