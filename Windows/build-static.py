#!/usr/bin/env python3
"""Build pinned Windows static libraries for MinGW or Visual Studio."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFINES = "CURL_STATICLIB FLAC__NO_DLL LIBXMP_STATIC MIKMOD_STATIC QSSM_STATIC_WINDOWS"
# All are Windows SDK import libraries. Third-party dependencies must be .a/.lib.
SYSTEM_LIBS = (
    "opengl32 winmm ws2_32 user32 gdi32 imm32 ole32 oleaut32 version uuid "
    "advapi32 setupapi shell32 shlwapi cfgmgr32 crypt32 secur32 normaliz "
    "bcrypt ncrypt iphlpapi comdlg32 dinput8"
)


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def write_if_changed(path, content):
    data = content.encode("utf-8")
    if not path.exists() or path.read_bytes() != data:
        # Keep shell scripts LF-terminated on Windows too.
        path.write_bytes(data)


def mingw_native():
    return os.name == "nt" or os.environ.get("MSYSTEM", "").startswith("MINGW")


def compiler_identity(args):
    def output(*command):
        return subprocess.check_output([str(a) for a in command], stderr=subprocess.STDOUT,
                                       encoding="utf-8", errors="replace").strip()

    if args.toolchain == "mingw":
        triplet = ("x86_64" if args.arch == "x64" else "i686") + "-w64-mingw32"
        cc, cxx = ("gcc", "g++") if mingw_native() else (triplet + "-gcc", triplet + "-g++")
        return {"cc": shutil.which(cc), "cxx": shutil.which(cxx),
                "cc_version": output(cc, "--version"), "cxx_version": output(cxx, "--version"),
                "target": output(cc, "-dumpmachine")}

    # CMake's VS generator works outside a Developer Command Prompt. Locate the
    # matching compiler without requiring cl.exe to already be in PATH.
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    instance = Path(output(vswhere, "-latest", "-version", "[17.0,18.0)", "-products", "*",
                           "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                           "-property", "installationPath"))
    version = instance / "VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt"
    if not version.is_file():
        raise RuntimeError("Visual Studio 2022 C++ desktop tools were not found")
    toolset = version.read_text().strip()
    compiler = instance / f"VC/Tools/MSVC/{toolset}/bin/Hostx64/{args.arch}/cl.exe"
    banner = subprocess.run([str(compiler), "/Bv"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace")
    # cl reports a missing input file with exit code 2 after printing versions.
    if banner.returncode not in (0, 2) or not banner.stdout.strip():
        raise RuntimeError(f"Could not query the MSVC compiler: {banner.stdout}")
    return {"instance": str(instance), "compiler": str(compiler), "toolset": toolset,
            "version": banner.stdout.strip()}


def compiler_fingerprint(identity):
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()


def fetch(dep, downloads):
    archive = downloads / dep["url"].rsplit("/", 1)[1]
    algorithm = "sha512" if "sha512" in dep else "sha256"
    if archive.exists() and hashlib.new(algorithm, archive.read_bytes()).hexdigest() == dep[algorithm]:
        return archive
    for url in [dep["url"], *dep.get("mirrors", [])]:
        with tempfile.NamedTemporaryFile(dir=downloads, suffix=".partial", delete=False) as output:
            partial = Path(output.name)
        try:
            run("curl", "--fail", "--location", "--retry", "3", "--max-time", "180",
                url, "--output", partial)
            if hashlib.new(algorithm, partial.read_bytes()).hexdigest() == dep[algorithm]:
                partial.replace(archive)
                return archive
        except subprocess.CalledProcessError:
            pass
        finally:
            partial.unlink(missing_ok=True)
    raise RuntimeError(f"Could not download a checksum-verified copy of {archive.name}")


def extract(archive, container):
    if not (container / ".qssm-extracted").is_file():
        # Publish only a complete extraction. An interrupted build must not reuse
        # a partially unpacked source tree on its next run.
        if container.exists():
            shutil.rmtree(container)
        container.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=container.parent, prefix=container.name + "-extract-") as tmp:
            staging = Path(tmp)
            with tarfile.open(archive) as tf:
                tf.extractall(staging, filter="data")
            roots = [p for p in staging.iterdir() if p.is_dir()]
            if len(roots) != 1:
                raise RuntimeError(f"Expected one source directory in {archive.name}")
            (staging / ".qssm-extracted").touch()
            staging.replace(container)
    return next(p for p in container.iterdir() if p.is_dir())


def apply_patches(source, patches):
    for patch in patches:
        # A Windows checkout can have CRLF patches while source archives use LF.
        # Normalize the patch stream without changing source context whitespace.
        data = patch.read_bytes().replace(b"\r\n", b"\n")
        # git apply is atomic per patch; a previous run may have finished some
        # patches before being interrupted. Check those before applying again.
        command = ["git", "apply", "--unsafe-paths", "--directory=" + source.as_posix()]
        applied = subprocess.run([*command, "--reverse", "--check", "-"], cwd=ROOT, input=data,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if applied.returncode:
            print(f"Applying {patch.name}", flush=True)
            run(*command, "-", input=data, cwd=ROOT)


def build_fingerprint(manifest, args, compiler):
    inputs = hashlib.sha256(json.dumps({
        "sources": manifest, "arch": args.arch, "toolchain": args.toolchain,
        "config": args.config, "platform": os.name, "checkout": str(ROOT),
        "compiler": compiler,
    }, sort_keys=True).encode())
    inputs.update((HERE / "build-static.py").read_bytes())
    for adapter in sorted((HERE / "static-adapters").iterdir()):
        if adapter.is_file():
            inputs.update(adapter.name.encode())
            inputs.update(adapter.read_bytes())
    return inputs.hexdigest()


def prepare_build(work, fingerprint):
    state = work / "build-inputs.sha256"
    if not state.exists() or state.read_text() != fingerprint:
        # Recipes and configuration affect Autotools stamps too. Start clean
        # when they change, or when restoring a cache without a valid state.
        for generated in (work / "prefix", work / "sources", work / "objects"):
            if generated.exists():
                shutil.rmtree(generated)
    (work / "prefix").mkdir(parents=True, exist_ok=True)
    write_if_changed(state, fingerprint)


def lock_build(work):
    """Prevent two builders from modifying the same architecture's objects."""
    lock = (work / "build.lock").open("a+b")
    try:
        if os.name == "nt":
            import msvcrt
            if lock.seek(0, os.SEEK_END) == 0:
                lock.write(b"0")
                lock.flush()
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        lock.close()
        raise RuntimeError(f"Another dependency build is using {work}") from None
    return lock


def build_gnutls(args, manifest, downloads, work, prefix, triplet, native):
    env = os.environ.copy()
    optimization = "-O0 -g" if args.config == "Debug" else "-O2"
    env.update(CC="gcc" if native else triplet + "-gcc",
               CXX="g++" if native else triplet + "-g++",
               AR="ar" if native else triplet + "-ar",
               RANLIB="ranlib" if native else triplet + "-ranlib",
               CFLAGS=f"{optimization} -std=gnu11 -Wno-error -Wno-cast-function-type",
               CXXFLAGS=f"{optimization} -std=gnu++11 -Wno-error -Wno-cast-function-type",
               CPPFLAGS=f"-I{prefix.as_posix()}/include",
               LDFLAGS=f"-L{prefix.as_posix()}/lib -static -static-libgcc",
               PKG_CONFIG_LIBDIR=f"{prefix.as_posix()}/lib/pkgconfig",
               PKG_CONFIG_PATH="")
    wrapper = work / "pkg-config-static"
    write_if_changed(wrapper, '#!/bin/sh\nexec pkg-config --static "$@"\n')
    wrapper.chmod(0o755)
    env["PKG_CONFIG"] = wrapper.as_posix()
    for name in ("gmp", "nettle", "tasn1", "gnutls"):
        dep = manifest[name]
        source = extract(fetch(dep, downloads), work / "sources" / name)
        stamp = prefix / (name + ".built")
        if stamp.exists() and stamp.read_text() == dep["sha256"]:
            continue
        build = work / "objects" / name
        build.mkdir(parents=True, exist_ok=True)
        flags = [f"--host={triplet}", f"--prefix={prefix.as_posix()}", "--disable-shared", "--enable-static"]
        if name == "nettle":
            flags += ["--disable-documentation"]
        if name == "tasn1":
            flags += ["--disable-doc", "--disable-gcc-warnings"]
        if name == "gnutls":
            header = source / "lib/crau/crau.h"
            if header.exists() and "#ifndef __has_c_attribute" not in header.read_text():
                header.write_text("#ifndef __has_c_attribute\n#define __has_c_attribute(x) 0\n#endif\n" + header.read_text())
            flags += ["--without-p11-kit", "--without-idn", "--without-zlib", "--with-included-unistring",
                      "--without-brotli", "--without-zstd", "--without-tpm", "--without-tpm2",
                      "--without-leancrypto", "--disable-libdane", "--disable-gcc-warnings",
                      "--disable-tools", "--disable-doc", "--disable-tests", "--disable-nls"]
        print(f"Building {name} {dep['version']} ({args.arch})", flush=True)
        run("bash", (source / "configure").as_posix(), *flags, cwd=build, env=env)
        if name == "nettle":
            run("make", f"-j{args.jobs}", "libnettle.a", "libhogweed.a", cwd=build, env=env)
            run("make", "install-static", "install-headers", "install-pkgconfig", cwd=build, env=env)
        elif name == "tasn1":
            run("make", f"-j{args.jobs}", cwd=build / "lib", env=env)
            run("make", "install", cwd=build / "lib", env=env)
        else:
            run("make", f"-j{args.jobs}", cwd=build, env=env)
            run("make", "install", cwd=build, env=env)
        stamp.write_text(dep["sha256"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x86", "x64"), required=True)
    parser.add_argument("--toolchain", choices=("mingw", "msvc"), default="mingw")
    parser.add_argument("--config", choices=("Release", "Debug"), default="Release")
    cpu_count = getattr(os, "process_cpu_count", os.cpu_count)
    parser.add_argument("--jobs", type=int, default=min(cpu_count() or 2, 8))
    parser.add_argument("--download-only", action="store_true")
    parser.add_argument("--print-toolchain-id", action="store_true",
                        help="print the compiler fingerprint for CI cache keys and exit")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    if args.toolchain == "mingw" and not args.download_only and any(c.isspace() for c in str(ROOT)):
        parser.error("Use a checkout path without whitespace for MinGW's GNU Autotools dependencies")
    compiler = None if args.download_only else compiler_identity(args)
    if args.print_toolchain_id:
        if compiler is None:
            parser.error("--print-toolchain-id cannot be combined with --download-only")
        print(compiler_fingerprint(compiler))
        return
    base = ROOT / "build/windows-static"
    downloads = base / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((HERE / "static-sources.json").read_text())
    if args.download_only:
        for dep in manifest.values():
            fetch(dep, downloads)
        return
    suffix = f"-{args.config}" if args.toolchain == "msvc" else ""
    work = base / f"{args.toolchain}-{args.arch}{suffix}"
    prefix = work / "prefix"
    prefix.mkdir(parents=True, exist_ok=True)
    build_lock = lock_build(work)
    prepare_build(work, build_fingerprint(manifest, args, compiler))
    triplet = ("x86_64" if args.arch == "x64" else "i686") + "-w64-mingw32"
    common = [f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}",
              f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}", "-DCMAKE_INSTALL_LIBDIR=lib",
              f"-DCMAKE_BUILD_TYPE={args.config}", "-DBUILD_SHARED_LIBS=OFF",
              "-DBUILD_TESTING=OFF", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
    if args.toolchain == "mingw":
        # Native MSYS2 has gcc in PATH; Linux uses the cross compiler.
        native = mingw_native()
        common += ["-G", "Ninja", "-DCMAKE_SYSTEM_NAME=Windows",
                   f"-DCMAKE_C_COMPILER={'gcc' if native else triplet + '-gcc'}",
                   f"-DCMAKE_CXX_COMPILER={'g++' if native else triplet + '-g++'}",
                   f"-DCMAKE_RC_COMPILER={'windres' if native else triplet + '-windres'}"]
    else:
        common += ["-G", "Visual Studio 17 2022", "-A", "x64" if args.arch == "x64" else "Win32",
                   f"-DCMAKE_GENERATOR_INSTANCE={compiler['instance']}",
                   "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
                   "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded" + ("Debug" if args.config == "Debug" else "")]
    options = {
        "sdl3": [
            "SDL_SHARED=OFF", "SDL_STATIC=ON", "SDL_TEST_LIBRARY=OFF",
            "SDL_TESTS=OFF", "SDL_EXAMPLES=OFF", "SDL_INSTALL_DOCS=OFF",
            "SDL_LIBC=OFF",
        ],
        "zlib": ["ZLIB_BUILD_SHARED=OFF", "ZLIB_BUILD_STATIC=ON", "ZLIB_BUILD_TESTING=OFF"],
        "curl": [
            "BUILD_SHARED_LIBS=OFF", "BUILD_STATIC_LIBS=ON", "BUILD_CURL_EXE=OFF",
            "BUILD_LIBCURL_DOCS=OFF", "BUILD_MISC_DOCS=OFF", "ENABLE_CURL_MANUAL=OFF",
            "CURL_USE_SCHANNEL=ON", "CURL_USE_OPENSSL=OFF", "CURL_WINDOWS_SSPI=ON",
            "CURL_USE_LIBPSL=OFF", "CURL_USE_LIBSSH2=OFF", "CURL_USE_LIBSSH=OFF",
            "USE_LIBIDN2=OFF", "USE_WIN32_IDN=ON", "USE_NGHTTP2=OFF",
            "CURL_BROTLI=OFF", "CURL_ZSTD=OFF", "CURL_USE_PKGCONFIG=OFF",
            "CURL_DISABLE_LDAP=ON", "CURL_DISABLE_LDAPS=ON", "CURL_ZLIB=ON",
            "CURL_STATIC_CRT=ON",
        ],
        "ogg": ["INSTALL_DOCS=OFF"],
        "vorbis": [],
        "flac": [
            "BUILD_CXXLIBS=OFF", "BUILD_PROGRAMS=OFF", "BUILD_EXAMPLES=OFF",
            "BUILD_DOCS=OFF", "INSTALL_MANPAGES=OFF", "ENABLE_MULTITHREADING=OFF",
            "WITH_STACK_PROTECTOR=ON",
        ],
        "opus": ["OPUS_BUILD_TESTING=OFF", "OPUS_BUILD_PROGRAMS=OFF", "OPUS_INSTALL_PKG_CONFIG_MODULE=ON"],
        "opusfile": [],
        "mad": [],
        "xmp": ["BUILD_SHARED=OFF", "BUILD_STATIC=ON", "WITH_UNIT_TESTS=OFF"],
        "mikmod": [
            "ENABLE_SHARED=OFF", "ENABLE_STATIC=ON", "ENABLE_DSOUND=OFF",
            "ENABLE_WASAPI=OFF", "ENABLE_WINMM=OFF", "ENABLE_XAUDIO2=OFF",
            "ENABLE_OPENAL=OFF", "ENABLE_SDL=OFF", "ENABLE_DOC=OFF",
        ],
        "mpg123": ["BUILD_LIBOUT123=OFF", "BUILD_PROGRAMS=OFF", "BUILD_TESTING=OFF"],
    }
    license_parts = []
    for name in options:
        dep = manifest[name]
        if name == "mpg123" and args.toolchain == "msvc":
            continue  # The VS project uses libmad; mpg123 is a MinGW makefile option.
        archive = fetch(dep, downloads)
        source_container = work / "sources" / name
        source = extract(archive, source_container)
        if name in ("mad", "opusfile"):
            write_if_changed(source / "CMakeLists.txt", (HERE / "static-adapters" / f"{name}.cmake").read_text())
        if name == "mad":
            # Preserve the decoder bounds fixes included in the old MSYS2 DLLs.
            marker = source / ".qssm-patched"
            if not marker.exists():
                apply_patches(source, sorted((HERE / "static-adapters").glob("mad-*.patch")))
                marker.touch()
        build = work / "objects" / name
        print(f"Building {name} {dep['version']} ({args.toolchain}, {args.arch})", flush=True)
        extra = list(options[name])
        if name == "curl":
            zlib = next(p for p in (prefix / "lib").iterdir()
                        if p.suffix in (".a", ".lib") and p.stem in ("libzs", "zs", "zsd"))
            extra += [f"ZLIB_LIBRARY={zlib.as_posix()}", f"ZLIB_INCLUDE_DIR={prefix.as_posix()}/include"]
        cmake_source = source / "ports/cmake" if name == "mpg123" else source
        run("cmake", "-S", cmake_source, "-B", build, *common, *("-D" + option for option in extra))
        run("cmake", "--build", build, "--config", args.config, "--parallel", args.jobs)
        run("cmake", "--install", build, "--config", args.config)
        for license in sorted(source.iterdir()):
            if license.is_file() and license.name.startswith(("COPYING", "LICENSE", "COPYRIGHT", "NOTICE")):
                license_parts.append(f"\n{'=' * 72}\n{name} {dep['version']}: {license.name}\n{dep['url']}\n\n" + license.read_text(encoding="utf-8", errors="replace"))
    if args.toolchain == "mingw":
        build_gnutls(args, manifest, downloads, work, prefix, triplet, native)
        for name in ("gmp", "nettle", "tasn1", "gnutls"):
            source = next(p for p in (work / "sources" / name).iterdir() if p.is_dir())
            for license in sorted(source.glob("COPYING*")):
                if license.is_file():
                    license_parts.append(f"\n{'=' * 72}\n{name}: {license.name}\n\n" + license.read_text(encoding="utf-8", errors="replace"))
        notice = HERE / "static-licenses/winpthreads-COPYING"
        license_parts.append(f"\n{'=' * 72}\nMinGW-w64 winpthreads: COPYING\n\n" + notice.read_text(encoding="utf-8"))
    write_if_changed(prefix / "third-party-licenses.txt", "".join(license_parts))
    write_if_changed(prefix / "static-sources.json", (HERE / "static-sources.json").read_text())
    # Refer to actual archives, never import libraries from the old DLL folders.
    if args.toolchain == "mingw":
        archives = sorted((prefix / "lib").glob("*.a"))
        if any(p.name.endswith(".dll.a") for p in archives):
            raise RuntimeError("An import library appeared in the static prefix")
        flags = ' '.join(f'"{p.as_posix()}"' for p in archives)
        write_if_changed(prefix / "qssm-static.mk",
            'WINDOWS_STATIC_ARCHIVES := ' + ' '.join(p.as_posix() for p in archives) + '\n' +
            f'WINDOWS_STATIC_CFLAGS := -I"{prefix.as_posix()}/include" -I"{prefix.as_posix()}/include/opus" ' +
            ' '.join('-D' + define for define in DEFINES.split()) + '\n' +
            f'WINDOWS_STATIC_LIBS := -Wl,--start-group {flags} -Wl,--end-group ' +
            ' '.join('-l' + lib for lib in SYSTEM_LIBS.split()) + ' -latomic -lssp -pthread\n')
    else:
        archives = sorted((prefix / "lib").glob("*.lib"))
        deps = ';'.join(str(p) for p in archives) + ';' + ';'.join(lib + '.lib' for lib in SYSTEM_LIBS.split())
        write_if_changed(prefix / "qssm-static.props",
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup>'
            '<ClCompile><AdditionalIncludeDirectories>' + escape(str(prefix / "include") + ';' + str(prefix / "include/opus")) + ';%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
            '<PreprocessorDefinitions>' + DEFINES.replace(' ', ';') + ';%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile>'
            '<Link><AdditionalDependencies>' + escape(deps) + ';%(AdditionalDependencies)</AdditionalDependencies></Link>'
            '</ItemDefinitionGroup></Project>')
    print(f"Static dependency prefix: {prefix}")
    build_lock.close()


if __name__ == "__main__":
    main()
