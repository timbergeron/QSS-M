# November 2026 dependency refresh

This refresh addresses dependency notifications [#87](https://github.com/timbergeron/QSS-M/issues/87)
through [#94](https://github.com/timbergeron/QSS-M/issues/94).

| Dependency | Updated version | Inputs |
| --- | --- | --- |
| SDL3 | 3.4.20 | Windows static source, legacy Windows SDK/DLLs, macOS universal framework, Linux CI pin |
| curl | 8.23.0 | Windows static source, legacy Schannel DLLs and public headers |
| Nettle | 4.0 | Windows static source; GnuTLS remains 3.8.13, which supports the Nettle 4 API |
| libmikmod | 3.3.16 | Windows static source, legacy DLLs and public header |
| zlib | 1.3.2-3 | Legacy MSYS2 DLL rebuild; static source remains upstream 1.3.2 |
| Opus | 1.6.1-2 | Legacy MSYS2 DLL rebuild; static source remains upstream 1.6.1 |
| winpthreads | 14.0.0.r598.ge3d18511f-1 | Legacy runtime DLLs; supersedes the r569 notification |
| libgcc | 16.2.0-6 | Legacy x86 runtime DLL |
| SDL_GameControllerDB | 07af1d50187e94c861c9f9f5d8a55b148f11306b | Exact November 2 upstream snapshot (supersedes the notification) with the QSS-M provenance marker |

Current Windows release builds use `static-sources.json`, not the retained
legacy DLLs. Package rebuild baselines describe the refreshed legacy binaries;
the source versions and hashes continue to describe the static build inputs.
The macOS crypto versions remain independently pinned in `macOS/setup-vcpkg.sh`.

## Provenance and licensing

SDL source and SDK/DMG archives were checked against SHA-256 digests from the
[official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20).
The SDL package hashes are also recorded in `Misc/sdl3/README.md`. The framework
and Windows SDK files match the official distributions, including the framework
symlinks and signature resources.

The curl, Nettle and libmikmod source hashes match the official MSYS2 PKGBUILDs;
`static-sources.json` records their upstream archive URLs and hashes. Legacy
curl is built with Schannel and Windows IDN, with zlib as its direct bundled
dependency.
The existing import libraries are retained after checking exported names.
The libmikmod x86 build uses Autotools to retain the four legacy driver exports
omitted by its CMake build; its recipe is in `codecs/include/mikmod_config.txt`.

The zlib package's minizip header installation patch does not affect QSS-M's
static zlib. The Opus package's ARM feature patch does not affect x86/x64.
The licenses remain SDL's zlib license, curl's permissive license, libmikmod's
LGPL, Nettle's GPL/LGPL alternatives, zlib's license, Opus's BSD license,
winpthreads' mingw-w64/Lockless notices, and libgcc's GCC runtime exception.
The runtime packages contain the same license terms; winpthreads differs only
in blank lines from the retained complete notice.

These runtime archives were verified against the SHA-256 fields in the MSYS2
mingw32 and mingw64 repository indexes downloaded on November 5, 2026:

| Package | SHA-256 |
| --- | --- |
| `mingw-w64-i686-libgcc-16.2.0-6-any.pkg.tar.zst` | `7b89855e761f98b16608160771976f12e4075b28224ff9405fcf09ed37ddec44` |
| `mingw-w64-i686-libwinpthread-14.0.0.r598.ge3d18511f-1-any.pkg.tar.zst` | `82c4085f14d417322901fa1fba4cfac77f398aa0b04dc653a0af7007746689f0` |
| `mingw-w64-i686-opus-1.6.1-2-any.pkg.tar.zst` | `f0bfd6f11ccfdaaa4d84119e9c6265e84595f980e49ff2ffcb457b5c165281de` |
| `mingw-w64-i686-zlib-1.3.2-3-any.pkg.tar.zst` | `586f3ec24c0c10e07ddd3bd3280bcf53ee45be7e96a80ee4a2712569c3fe8213` |
| `mingw-w64-x86_64-libmikmod-3.3.16-1-any.pkg.tar.zst` | `13a1947e8775ab4d4cc685f755ebcaef1089ec5f285ef997b231680856ae882a` |
| `mingw-w64-x86_64-libwinpthread-14.0.0.r598.ge3d18511f-1-any.pkg.tar.zst` | `942a62a3ecafbb4f5a80016685a0e3002a8de903012eed49c213e4f5b693668a` |
| `mingw-w64-x86_64-opus-1.6.1-2-any.pkg.tar.zst` | `fae981c518ed1ff2c5264eb5a0d590ec1d2ebb222defb2253ca85b2154fd8c04` |
| `mingw-w64-x86_64-zlib-1.3.2-3-any.pkg.tar.zst` | `ffde6f4ed2d879477c93b37ff14c319baf480a2aeafc065edeceafba26b71e90` |

## Validation

- All 38 dependency-workflow tests and all 14 existing Windows static-build tests pass.
- All 16 static source archives match their pinned SHA-256 hashes.
- Clean MinGW source builds pass for x86 and x64, including GnuTLS 3.8.13
  against Nettle 4.0.
- Both Windows release scripts build the engine and pass PE import and ZIP
  verification. Both executables import only system DLLs; both ZIPs contain
  the current source manifest, controller mappings and dependency licenses,
  with no DLLs.
- The MSYS2 comparison, including monthly dependencies, reports no changes
  against the refreshed baselines. The controller snapshot matches the upstream commit byte for byte
  after removing its provenance marker; SDL accepts all 2,282 mappings.
- Updated legacy DLLs retain the previous exported names. Every retained DLL
  has the expected PE architecture and complete transitive dependency closure,
  including the required sibling export names. No UCRT dependency is introduced. The x86 zlib rebuild now needs the already
  bundled libgcc and winpthreads DLLs, as documented in `zlib/README.md`.
- Both macOS framework slices retain all 1,271 exports and the existing minimum
  deployment targets (x86_64 10.13, arm64 11.0).

Native Windows gameplay/VoIP testing and an Xcode/macOS runtime test require
those operating systems; the Linux refresh host cannot perform them.

## November 18 follow-up

This follow-up addresses dependency notifications [#96](https://github.com/timbergeron/QSS-M/issues/96)
through [#99](https://github.com/timbergeron/QSS-M/issues/99). Every notice was
an MSYS2 package rebuild with an unchanged upstream version.

| Dependency | Baseline | Action |
| --- | --- | --- |
| libFLAC | 1.5.0-3 | x64 legacy DLL refreshed; the recipe change only patches the CMake config module. MSYS2 no longer publishes mingw32 FLAC, so the x86 DLL stays at 1.5.0-2 |
| libopusfile | 0.12-5 | x64 legacy DLL refreshed; the rebuild was for OpenSSL 4, which only affects the unvendored `libopusurl-0.dll`. The x86 DLL is unchanged |
| libgcc | 16.2.0-7 | x86 legacy runtime DLL refreshed; the recipe change only removes unused configs |
| curl | 8.23.0-3 | Baseline only; MSYS2 changed packaging and rebuilt for OpenSSL 4. QSS-M builds curl itself with Schannel from the unchanged 8.23.0 source |
| libtasn1 | 4.21.0-2 | Baseline only; the recipe change only affects documentation and build dependencies. The static source is unchanged |

The runtime archives were verified against the SHA-256 fields in the MSYS2
repository indexes downloaded on November 18, 2026:

| Package | SHA-256 |
| --- | --- |
| `mingw-w64-i686-libgcc-16.2.0-7-any.pkg.tar.zst` | `a6681c159f7cf4224b582a70f2816e05af2c89dfb9d407b26b817672cc95c943` |
| `mingw-w64-x86_64-flac-1.5.0-3-any.pkg.tar.zst` | `5da0db245b8e8c20899a74f72dfaa0229c682f372255d61299dd6bdf72239e75` |
| `mingw-w64-x86_64-opusfile-0.12-5-any.pkg.tar.zst` | `f9c5451d7d3d3e66a5efc14dc3bfa7f224cff691909ea666552520850696ced6` |

The refreshed DLLs keep the same PE architecture, the same imports and the
same exported names. Every symbol they import from sibling DLLs resolves against
the bundled `libogg-0.dll`, `libopus-0.dll` and `libwinpthread-1.dll`. No UCRT
dependency is introduced, so the existing import libraries are retained. The
license terms are unchanged.
