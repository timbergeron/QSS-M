# Windows codec runtime DLLs

Prebuilt Windows codec libraries vendored for the MinGW and MSVC builds.
`x64/` holds the 64-bit binaries, `x86/` the 32-bit ones, and `include/` the
shared public headers.

All DLLs are **MSVCRT-based** MinGW builds (they import `msvcrt.dll`, not the
UCRT `api-ms-win-crt-*` DLLs). Keep it that way: every other bundled DLL —
SDL2, curl, zlib, the whole codec set, and the main `QSS-M-w*.exe` (built by
`MSYS2.yml` / `build-w*.sh` on the MINGW toolchain) — is MSVCRT, and MSVCRT is
present on all supported Windows versions. A UCRT DLL would be the odd one out
and would need Windows 10+ (or the UCRT redistributable on 7/8.1).

## libFLAC dependency trap (read before bumping FLAC)

The old self-contained `libFLAC-8.dll` (1.3.0, 2013) imported only `KERNEL32`
and `msvcrt`, so it was a drop-in file. **Modern FLAC is not.** A current
prebuilt `libFLAC.dll` pulls in a chain of transitive dependencies, and if any
are missing the DLL **fails to load silently** — FLAC music just stops working
with no error pointing at the cause. This is almost certainly why the Windows
build sat on 1.3.0 for so long: naive drop-in updates broke mysteriously.

Full dependency closure that MUST be vendored alongside `libFLAC.dll`:

```
libFLAC.dll  ->  libogg-0.dll         (also used by Vorbis/Opus)
                 libwinpthread-1.dll  (1.5.0's multithreaded encoder)
                 libgcc_s_dw2-1.dll   (x86 ONLY -- dwarf2 exception runtime)
                 KERNEL32.dll, msvcrt.dll   (system)
```

- `libwinpthread-1.dll` is vendored in **both** `x64/` and `x86/`.
- `libgcc_s_dw2-1.dll` is **x86 only** — the 64-bit DLL uses SEH and needs no
  libgcc. (`libgcc_s_dw2-1.dll` itself also needs `libwinpthread-1.dll`.)

Verify the closure after any change:

```sh
# every import must be KERNEL32/msvcrt (system) or a sibling DLL in this dir
x86_64-w64-mingw32-objdump -p x64/libFLAC.dll | grep 'DLL Name'
i686-w64-mingw32-objdump   -p x86/libFLAC.dll | grep 'DLL Name'
```

## Currently vendored versions

| Library            | Version        | Source (MSYS2 package)                     |
|--------------------|----------------|--------------------------------------------|
| libFLAC            | 1.5.0          | `mingw-w64-{x86_64,i686}-flac-1.5.0`       |
| libopus            | 1.6.1          | `mingw-w64-{x86_64,i686}-opus-1.6.1`       |
| libopusfile        | 0.12           | `mingw-w64-{x86_64,i686}-opusfile-0.12`    |
| libxmp             | 4.7.1          | `mingw-w64-{x86_64,i686}-libxmp-4.7.1`     |
| libmikmod          | 3.3.13         | `mingw-w64-{x86_64,i686}-libmikmod-3.3.13` |
| libmpg123          | 1.33.5         | `mingw-w64-{x86_64,i686}-mpg123-1.33.5`    |
| libogg             | 1.3.6          | `mingw-w64-{x86_64,i686}-libogg-1.3.6`     |
| libwinpthread      | 12.0.0.r747    | `mingw-w64-{x86_64,i686}-libwinpthread-git`|
| libgcc (x86 only)  | gcc 16.1.0     | `mingw-w64-i686-gcc-libs`                  |

Note: modern libopus/libopusfile also import `libgcc_s_dw2-1.dll` on x86 — the
same DLL FLAC needs, so no additional runtime file. `libopusfile.dll` needs
`libogg-0.dll` + `libopus-0.dll`; it does **not** need `libopusurl-0.dll` (HTTP
streaming is unused, so that DLL is intentionally not vendored).

Note: libxmp 4.7.x also picked up the x86 `libgcc_s_dw2-1.dll` dependency (4.6.x
was self-contained), again already covered by the FLAC vendoring. Unlike opus,
libxmp's version is reported from the `XMP_VERSION` macro in `include/xmp.h`, so
that header **must** be refreshed alongside the DLL (host.c/menu.c pick it up
automatically). The macOS `xmp.h` is a separate platform-specific copy and
should only be updated alongside the macOS libxmp build, not to match Windows.

Note: libmikmod is disabled in the MinGW and `Makefile.darwin` builds
(`USE_CODEC_MIKMOD=0`), where libxmp handles module playback. It remains enabled
in the MSVC project and macOS Xcode target; macOS uses its separate dylib and
header. The Windows 3.3.13 DLL is built with DirectSound/WinMM output drivers, so
it imports `dsound.dll` / `user32.dll` / `winmm.dll` (all system DLLs) — these
are inert here: `snd_mikmod.c` registers only `drv_nos` and decodes via
`VC_WriteBytes`. Version is header-driven (`LIBMIKMOD_VERSION_*` in
`include/mikmod.h`); on x86 it needs the shared `libgcc_s_dw2-1.dll`.

Note: libmpg123 is **not linked by default** — MP3 decoding uses libmad
(`MP3LIB=mad` in the makefiles, `libmad` in the MSVC project); libmpg123 is only
used with `MP3LIB=mpg123`, which the makefiles signal to the C code by defining
`-DMP3LIB_MPG123`. The version surfaces (`Quake/host.c`, `Quake/menu.c`) use that
define to pick the mad vs mpg123 branch and report the mpg123 version at runtime
via `mpg123_distversion()` (guarded by `MPG123_API_VERSION >= 48`, since that
function only exists from mpg123 1.32; older headers fall back to printing the
API version) — so no version string needs manual bumping. mpg123
1.32+ split `fmt123.h` out of `mpg123.h`, so both headers are vendored together.
The DLL imports `SHLWAPI.dll` (system) and, on x86, the shared
`libgcc_s_dw2-1.dll`.

The Windows import libraries (`libFLAC.dll.a` for MinGW, `libFLAC.lib` for MSVC)
target `libFLAC.dll` and were generated with `dlltool` from the shipped DLL.
Import libraries can be kept when the DLL name and required exported symbols
remain ABI-compatible. That was verified for this opus/opusfile update, so the
existing import libraries were retained. The hand-patched opus headers, which
use `<opus/...>` include paths to match the `-Iinclude` build flag, were also
kept as-is.

## Updating a codec DLL

1. Download the MSYS2 `mingw64` (x64) and `mingw32` (x86) packages from
   `https://repo.msys2.org/mingw/{mingw64,mingw32}/` and extract the `.dll`
   (they are `.pkg.tar.zst`; `tar -xf` handles them).
2. Copy the DLLs into `x64/` and `x86/`. For FLAC, regenerate the import libs:
   ```sh
   x86_64-w64-mingw32-objdump -x x64/libFLAC.dll | grep -oE 'FLAC__[A-Za-z0-9_]+' \
     | sort -u > flac.def.body   # build a LIBRARY/EXPORTS .def, then:
   x86_64-w64-mingw32-dlltool -d flac.def -D libFLAC.dll -l x64/libFLAC.dll.a
   x86_64-w64-mingw32-dlltool -d flac.def -D libFLAC.dll -l x64/libFLAC.lib
   ```
3. Verify the dependency closure (see above) and vendor any new transitive deps.
4. Update the copies of the public headers in `include/` (and the mirror in
   `MacOSX/codecs/include/`) if the ABI/version changed.
5. Keep these manifests in sync — packaging globs `codecs/*/*.dll`, but these
   lists are explicit:
   - `Quake/update.c` — `update_win_files[]` and `update_win_helper_runtime_files[]`
     (the in-app auto-updater). Missing files are tolerated per-arch, so an
     x86-only DLL listed here is simply skipped on win64.
   - `.github/workflows/msvs.yml` — the required-runtime-file assertion in the
     "Prepare archive" step.
   - Hardcoded version strings (libraries without a runtime version API):
     `Quake/host.c` (`version` command) and `Quake/menu.c` (version menu).
