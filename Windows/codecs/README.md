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
| libogg             | 1.3.6          | `mingw-w64-{x86_64,i686}-libogg-1.3.6`     |
| libwinpthread      | 12.0.0.r747    | `mingw-w64-{x86_64,i686}-libwinpthread-git`|
| libgcc (x86 only)  | gcc 16.1.0     | `mingw-w64-i686-gcc-libs`                  |

The MSVC import libraries (`libFLAC.dll.a` for MinGW, `libFLAC.lib` for MSVC)
target `libFLAC.dll` and were generated with `dlltool` from the shipped DLL.

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
