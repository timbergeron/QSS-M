# macOS Build

This directory contains the current macOS packaging and launcher setup for `QSS-M`.
The active macOS project is [QuakeSpasm.xcodeproj](./QuakeSpasm.xcodeproj), the launcher
sources live under [Sources](./Sources), and app resources live under
[Resources](./Resources).

Current repo layout:

- `QuakeSpasm.xcodeproj`: Xcode project for the `QSS-M` app target.
- `Sources/Launcher`: Objective-C launcher code.
- `Sources/SDL`: macOS SDL app bootstrap code used by the launcher target.
- `Resources/Info.plist`: app bundle metadata.
- `Resources/Base.lproj/Launcher.xib`: launcher UI source. Xcode compiles this into `Launcher.nib` at build time.
- `Resources/QuakeSpasm.icns`: app icon.
- `SDL2.framework`, `codecs/`, and `custom-triplets/`: repo-managed macOS build inputs.
- `vcpkg/`, `libs_universal/`, and `build/`: generated locally by the helper scripts and ignored by git.

## Output

The macOS build produces a universal app bundle for:

- `x86_64` with a deployment target of macOS `10.13`
- `arm64` with a deployment target of macOS `11.0`

The active project uses SDL 2. The old checked-in SDL 1.2 nib flow is no longer part of the build.

## Prerequisites

- Xcode and the Xcode command line tools
- Homebrew
- Required tools on `PATH`: `git`, `lipo`, `autoconf`, `automake`, `pkg-config`
- GNU `libtoolize`
- `autoconf-archive`

Install the Homebrew pieces with:

```sh
brew install autoconf automake libtool pkg-config autoconf-archive
```

Notes:

- `setup-vcpkg.sh` must be run as a normal user, not with `sudo`.
- On macOS, Homebrew usually installs GNU libtool as `glibtoolize`. The script will create a local `libtoolize` shim automatically when needed.

## Dependency Setup

From the `MacOSX/` directory:

```sh
./setup-vcpkg.sh
```

What this script does:

- Pins `vcpkg` to a known commit instead of following upstream `master`
- Recreates `MacOSX/vcpkg/` if the checkout is missing or at the wrong commit
- Overrides the pinned crypto ports with the checked versions and archive hashes in `setup-vcpkg.sh`
- Builds static dependencies for `x64-osx-1013` and `arm64-osx-11`
- Combines those per-arch libraries into universal archives under `libs_universal/`

The generated `vcpkg/` checkout and `libs_universal/` output are local build artifacts and should not be committed.

## Build

Recommended release build:

```sh
./build-macos.sh
```

This helper script:

- Runs `setup-vcpkg.sh`
- Builds the `QSS-M` target in `Release`
- Writes `build/Release/Quakespasm-Spiked-Revision.txt`
- Packages the release output as `build/Release/QSS-M-macOS.zip`

Direct Xcode command-line builds also work:

```sh
xcodebuild -project QuakeSpasm.xcodeproj -target QSS-M -configuration Debug
xcodebuild -project QuakeSpasm.xcodeproj -target QSS-M -configuration Release
```

Build products are written under `MacOSX/build/`.

## Versioning Notes

- `build-macos.sh` sources `../ci-version.sh`
- If `QSSM_VERSION_SUFFIX` is set, the build passes that suffix into Xcode as `QSSM_VER_SUFFIX`
- The app bundle `CFBundleShortVersionString` is synchronized from `Quake/quakedef.h` during the Xcode build

## Current Status

- The launcher UI is maintained as a text XIB at `Resources/Base.lproj/Launcher.xib`
- There are no checked-in compiled launcher nibs in the active build path
- The app Help menu entry is wired in the launcher UI and Objective-C controller code, not in a generated binary nib
