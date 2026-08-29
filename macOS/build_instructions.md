# Building QSS-M on macOS

QSS-M uses an Xcode project and a pinned vcpkg registry checkout to produce a
universal macOS application. The supported build contains both Intel and Apple
Silicon executables:

| Architecture | Minimum macOS version | vcpkg triplet |
|---|---:|---|
| `x86_64` | 12.0 | `x64-osx-1013` |
| `arm64` | 12.0 | `arm64-osx-11` |

The triplet names keep their original deployment targets; static dependencies
built for an older minimum link into the 12.0 application unchanged.

The old Darwin makefile and pre-Xcode cross-compilation scripts are no longer
supported.

## Quick start

Install Xcode and its command-line tools, then install the required build tools:

```sh
brew install autoconf automake libtool pkg-config autoconf-archive
```

Build from the `macOS/` directory as a normal user:

```sh
./build-macos.sh
```

Do not run the setup or build scripts with `sudo`. The release application and
archive are written to:

- `build/Release/QSS-M.app`
- `build/Release/QSS-M-macOS.zip`

## Build inputs

The maintained macOS build consists of:

- `QuakeSpasm.xcodeproj`: the `QSS-M` application target.
- `Configurations/`: shared build policy plus the Debug and Release settings.
- `Sources/Launcher/`: the Objective-C launcher and macOS integration.
- `Sources/SDL/`: the customized SDL application bootstrap.
- `Resources/`: the app metadata, launcher UI, icon, and packaged resources.
- `custom-triplets/`: the Intel and Apple Silicon vcpkg configurations.
- `setup-vcpkg.sh`: dependency checkout, port overrides, builds, and universal
  archive creation.
- `sync-bundle-version.sh`: strict synchronization of the app version and build
  number from `Quake/quakedef.h`.
- `build-macos.sh`: dependency setup, clean release build, revision metadata,
  ZIP packaging, and release validation.
- `verify-macos-release.sh`: validates the app and packaged ZIP metadata,
  architectures, deployment targets, dependencies, and code-signature integrity.
- `SDL2.framework`: the one separately bundled third-party binary. SDL2 is not
  currently built by the vcpkg setup.

The following directories are generated locally and ignored by Git:

- `vcpkg/`: the pinned registry checkout and per-architecture installations.
- `libs_universal/`: static libraries combined with `lipo` for Xcode.
- `build/`: Xcode products and release archives.

## Dependency model

macOS dependency handling is intentionally hybrid:

| Dependency group | Source and version policy |
|---|---|
| GnuTLS, libtasn1, Nettle | Explicit versions and source hashes in `setup-vcpkg.sh`: GnuTLS 3.8.13, libtasn1 4.21.0, and Nettle 3.10.2. |
| FLAC, Ogg, Opus, Opusfile, Vorbis, libmad, libxmp, zlib | Built from the ports supplied by the pinned vcpkg registry baseline. Their versions are not overridden locally. |
| SDL2 | Checked-in universal `SDL2.framework` 2.32.10, linked and embedded by Xcode. |
| curl, libiconv, Apple frameworks | Supplied by the macOS SDK or operating system. |

This differs from the Windows build, which vendors its codec DLLs and matching
headers. macOS codec headers and libraries come from the same vcpkg installation,
preventing header/library version drift.

## Dependency setup

Run dependency setup directly when working on the Xcode project:

```sh
./setup-vcpkg.sh
```

The script:

1. Checks out the vcpkg commit declared by `VCPKG_COMMIT`.
2. Applies the checked security-library versions and archive hashes.
3. Removes installed security packages whose versions no longer match.
4. Builds static libraries for both custom triplets.
5. Combines the architecture-specific archives under `libs_universal/`.

The checkout is recreated automatically if it is missing, incomplete, or at the
wrong commit. Homebrew installs GNU libtool as `glibtoolize`; when necessary, the
script creates a temporary local `libtoolize` shim for ports that expect that
name.

## Building

`build-macos.sh` is the release and CI-equivalent entry point. It runs dependency
setup, performs a clean build of the `QSS-M` Release target, writes revision
metadata, copies the controller database, creates the release ZIP, and validates
the exact packaged artifact.

For development, run setup once and invoke Xcode directly:

```sh
xcodebuild -project QuakeSpasm.xcodeproj -target QSS-M -configuration Debug
xcodebuild -project QuakeSpasm.xcodeproj -target QSS-M -configuration Release
```

Opening `QuakeSpasm.xcodeproj` in Xcode uses the same generated dependencies.

## Versioning

- `build-macos.sh` sources `../ci-version.sh`.
- `QSSM_VERSION_SUFFIX`, when set, is passed to Xcode as `QSSM_VER_SUFFIX`.
- `Quake/quakedef.h` is the single source of truth for the release version.
  The Xcode build replaces the placeholder bundle metadata with its
  `QSSM_VER_MAJOR`, `QSSM_VER_MINOR`, and `QSSM_VER_PATCH` values, deriving a
  monotonically increasing `CFBundleVersion` as
  `major * 1000000 + minor * 1000 + patch`. Minor and patch components must
  remain below 1000 so that encoding stays monotonic.
- Release validation compares the built application and archive directly with
  `Quake/quakedef.h`; no matching version edit is required in an Xcode config.
- Release builds write `build/Release/Quakespasm-Spiked-Revision.txt` with the
  Git revision and compile date.

## Distribution signing

The default build is ad-hoc signed so macOS can verify bundle integrity. It is
not Developer ID signed or notarized, so downloaded archives may still require
the quarantine-removal step documented in `macos_instructions.html`. A
Gatekeeper-clean public distribution requires a separate credentialed signing
and notarization pipeline.

## Troubleshooting

- The examples assume a shell in `macOS/`, but both helper scripts resolve and
  use their own directory when invoked through another path.
- If setup reports a missing tool, confirm the Homebrew packages from the quick
  start are installed and available on `PATH`.
- If a dependency version or triplet changes, rerun `setup-vcpkg.sh` before
  building in Xcode.
- Never commit `vcpkg/`, `libs_universal/`, or `build/`; they are reproducible
  local outputs.
