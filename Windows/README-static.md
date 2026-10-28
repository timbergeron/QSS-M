# Static Windows builds

Windows release executables embed SDL3, curl, zlib, and the audio decoders.
MinGW builds also embed GnuTLS, GMP, Nettle, and libtasn1. The executables use the Windows system and
graphics driver DLLs installed on the computer.

`static-sources.json` pins the source versions and checksums. The build downloads
and verifies those archives, builds actual static libraries, and installs matching
headers into `build/windows-static/`. The old DLL/import-library directories are
not inputs to these builds. `static-adapters/` supplies CMake builds for libmad
and opusfile; libmad retains the MSYS2 decoder bounds fixes.

## Linux cross builds

Install MinGW-w64, Python 3.12+, CMake, Ninja, make, pkg-config, curl, xz,
autoconf, automake, libtool, gettext, texinfo, gperf, bison, flex, and zip.
Use a checkout path without spaces for the GNU Autotools dependencies. Then:

```sh
bash Misc/qssm_pak/build.sh
sh build-w64.sh
sh build-w32.sh
```

The first build compiles dependencies from source. Later builds reuse their
objects. Changes to source pins, patches, build recipes, compiler versions, or configuration
invalidate the dependency cache automatically.
Each script produces `Quake/QSS-M-w{64,32}.exe` and its ZIP, containing
the executable, game support files, documentation, dependency licenses, and
source manifest, with no bundled DLLs.

For direct makefile use, build dependencies first:

```sh
python3 Windows/build-static.py --arch x64
cd Quake
./build_cross_win64.sh -j8
```

Use `--arch x86` and `build_cross_win32.sh` for 32-bit builds.

## MSYS2

Use the MINGW64 shell for x64 or MINGW32 for x86. Install `base-devel`, `make`,
`git`, `curl`, `zip`, and the corresponding `mingw-w64-{x86_64,i686}-` packages
for `gcc`, `binutils`, `cmake`, `ninja`, `python`, and `pkgconf`.
UCRT64 is not supported: verification rejects its `api-ms-win-crt-*` runtime
imports. Use MINGW64 or MINGW32.
Use a checkout path without spaces and build from the repository root:

```sh
export CC=gcc CXX=g++ WINDRES=windres STRIP=strip
bash Misc/qssm_pak/build.sh
sh build-w64.sh  # build-w32.sh in MINGW32
```

## Visual Studio 2022

Install Python 3.12+, CMake, Git, and the Visual Studio C++ desktop workload.
From the repository root:

```bat
python Windows/build-static.py --toolchain msvc --arch x64
msbuild Windows\VisualStudio\quakespasm.vcxproj -p:Configuration=Release -p:Platform=x64
```

For Win32 use `--arch x86` and `-p:Platform=Win32`. For Debug, pass
`--config Debug` to the dependency build and select Debug in Visual Studio.
Both the application and its libraries use the static CRT (`/MT` or `/MTd`).
The generated `qssm-static.props` supplies archive paths and headers. The existing
module decoder choices are retained. GnuTLS remains optional in the Visual
Studio project, as before; its existing custom-library properties
must reference static libraries if enabled.

## Verification and upgrades

`verify-static.py` examines normal and delayed PE imports and rejects bundled
library DLLs and redistributable compiler runtimes. The release scripts also
verify the actual ZIP and reject any DLL it contains. CI runs these checks for
both architectures and all three toolchains.

Opus VoIP calls the embedded encoder and decoder in static Windows builds;
it does not load `libopus-0.dll`. Before shipping, test transmit and receive
in-game on Windows with no third-party DLLs beside the executable. Repeat with
an old `libopus-0.dll` present and check the process's loaded modules to confirm
that it is not used. PE import verification alone cannot detect runtime loads.

The MinGW license bundle includes the toolchain's winpthreads notices from
`static-licenses/winpthreads-COPYING`.

Installed versions whose updater requires SDL3.dll and libcurl.dll need one
manual upgrade: quit QSS-M and extract the complete static package into the
installation directory. The new updater accepts subsequent static packages.
Old DLL files can remain unused beside the static executable; the updater does
not delete files from an existing installation.

When updating a dependency, change its version, URL, and checksum in the source
manifest, rebuild both architectures, and rerun import/package verification.
Keep `.github/dependency-watch.json` synchronized with the versions in use.
