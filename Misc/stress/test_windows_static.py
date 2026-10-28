"""Exercise static dependency recovery, PE imports, and updater validation."""

import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("verify_static", ROOT / "Windows/verify-static.py")
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)
BUILD_SPEC = importlib.util.spec_from_file_location("build_static", ROOT / "Windows/build-static.py")
BUILD = importlib.util.module_from_spec(BUILD_SPEC)
BUILD_SPEC.loader.exec_module(BUILD)


def pe_fixture(dll, bits=64, delay=False):
    """A PE image with one .rdata section and one import descriptor."""
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    optional_size = 240 if bits == 64 else 224
    struct.pack_into("<HH", data, 0x84, 0x8664 if bits == 64 else 0x14c, 1)
    struct.pack_into("<H", data, 0x94, optional_size)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20b if bits == 64 else 0x10b)
    struct.pack_into("<I", data, optional + 60, 512)
    dirs = optional + (112 if bits == 64 else 96)
    struct.pack_into("<I", data, dirs - 4, 16)
    struct.pack_into("<II", data, dirs + (13 if delay else 1) * 8, 0x1000, 64 if delay else 40)
    section = optional + optional_size
    struct.pack_into("<IIII", data, section + 8, 512, 0x1000, 512, 512)
    if delay:
        struct.pack_into("<II", data, 512, 1, 0x1100)
    else:
        struct.pack_into("<I", data, 524, 0x1100)
    encoded = dll.encode("ascii") + b"\0"
    data[768:768 + len(encoded)] = encoded
    return bytes(data)


class StaticImportTests(unittest.TestCase):
    def test_system_imports_on_both_architectures(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "engine.exe"
            for bits in (32, 64):
                for delay in (False, True):
                    exe.write_bytes(pe_fixture("KERNEL32.dll", bits, delay))
                    VERIFY.verify(exe)

    def test_bundled_and_compiler_dlls_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "engine.exe"
            for name in ("SDL3.dll", "libcurl.dll", "libogg-0.dll", "libwinpthread-1.dll",
                         "libgcc_s_seh-1.dll", "VCRUNTIME140.dll", "api-ms-win-crt-runtime-l1-1-0.dll"):
                for delay in (False, True):
                    exe.write_bytes(pe_fixture(name, delay=delay))
                    with self.assertRaisesRegex(ValueError, "Non-system DLL"):
                        VERIFY.verify(exe)

    def test_package_cannot_hide_a_dll(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "engine.exe"
            exe.write_bytes(pe_fixture("KERNEL32.dll"))
            archive = Path(tmp) / "package.zip"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("runtime/SDL3.DLL", b"unexpected")
            with self.assertRaisesRegex(ValueError, "contains DLLs"):
                VERIFY.verify(exe, archive)


class WindowsVoipTests(unittest.TestCase):
    def test_windows_static_voip_uses_embedded_opus_encoder_and_decoder(self):
        """Compile the actual VoIP source; PE imports cannot catch runtime loads."""
        expected = {"opus_encoder_create", "opus_encoder_destroy", "opus_encoder_ctl", "opus_encode",
                    "opus_decoder_create", "opus_decoder_destroy", "opus_decoder_ctl", "opus_decode"}
        available = False
        for triplet in ("x86_64-w64-mingw32", "i686-w64-mingw32"):
            if not shutil.which(triplet + "-gcc") or not shutil.which(triplet + "-nm"):
                continue
            available = True
            with self.subTest(triplet=triplet), tempfile.TemporaryDirectory() as tmp:
                obj = Path(tmp) / "voip.o"
                command = [triplet + "-gcc", "-std=gnu11", "-D_USE_WINSOCK2", "-DUSE_CODEC_OPUS",
                           "-IWindows/SDL3/include", "-IWindows/codecs/include",
                           "-IWindows/codecs/include/opus", "-IQuake",
                           "-c", "Quake/snd_voip.c", "-o", str(obj)]
                # Keep the legacy DLL path functional for non-static builds.
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b"libopus-0.dll", obj.read_bytes())
                result = subprocess.run([*command, "-DQSSM_STATIC_WINDOWS"], cwd=ROOT,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                symbols = subprocess.check_output([triplet + "-nm", "-u", str(obj)], text=True)
                direct = set(re.findall(r"\bU\s+_?(opus_\w+)", symbols))
                self.assertTrue(expected <= direct, symbols)
                self.assertNotIn("__imp_opus_", symbols)
                self.assertNotIn("__imp__opus_", symbols)
                self.assertNotIn(b"libopus-0.dll", obj.read_bytes())
        if not available:
            self.skipTest("MinGW cross compiler and nm required")


class DependencyBuildTests(unittest.TestCase):
    def test_cache_tracks_recipe_configuration_sources_and_patches(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            recipe = root / "build-static.py"
            recipe.write_text("original recipe")
            adapters = root / "static-adapters"
            adapters.mkdir()
            patch = adapters / "decoder.patch"
            patch.write_text("original patch")
            args = SimpleNamespace(arch="x64", toolchain="mingw", config="Release")
            compiler = {"version": "original compiler"}
            manifest = {"decoder": {"version": "1.0"}}
            work = root / "work"
            work.mkdir()
            (work / "build.lock").touch()
            with mock.patch.object(BUILD, "HERE", root):
                def prepare():
                    BUILD.prepare_build(work, BUILD.build_fingerprint(manifest, args, compiler))

                prepare()
                archive = work / "prefix/libdecoder.a"
                archive.touch()
                prepare()
                self.assertTrue(archive.exists(), "matching inputs should reuse the cache")
                for change in (lambda: recipe.write_text("new recipe"),
                               lambda: patch.write_text("new patch"),
                               lambda: manifest["decoder"].update(version="2.0"),
                               lambda: setattr(args, "config", "Debug"),
                               lambda: compiler.update(version="updated compiler")):
                    for directory in ("sources", "objects"):
                        (work / directory).mkdir()
                        (work / directory / "stale").touch()
                    change()
                    prepare()
                    self.assertFalse(archive.exists())
                    self.assertFalse((work / "sources").exists())
                    self.assertFalse((work / "objects").exists())
                    self.assertTrue((work / "build.lock").exists())
                    archive.touch()
                (work / "build-inputs.sha256").unlink()
                prepare()
                self.assertFalse(archive.exists(), "a cache without state must be rebuilt")

    def test_unchanged_generated_files_keep_timestamps_and_use_lf(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ("qssm-static.mk", "qssm-static.props", "third-party-licenses.txt"):
                path = Path(tmp) / name
                BUILD.write_if_changed(path, "original\n")
                os.utime(path, (1000000000, 1000000000))
                before = path.stat().st_mtime_ns
                BUILD.write_if_changed(path, "original\n")
                self.assertEqual(path.stat().st_mtime_ns, before)
                BUILD.write_if_changed(path, "changed\n")
                self.assertNotEqual(path.stat().st_mtime_ns, before)
                self.assertEqual(path.read_bytes(), b"changed\n")

    def test_extraction_recovers_from_interrupted_and_failed_runs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / "source.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                info = tarfile.TarInfo("decoder/source.c")
                content = b"complete source"
                info.size = len(content)
                tar.addfile(info, io.BytesIO(content))
            container = root / "sources/decoder"
            (container / "decoder").mkdir(parents=True)
            (container / "decoder/source.c").write_text("partial source")
            source = BUILD.extract(archive, container)
            self.assertEqual((source / "source.c").read_bytes(), content)
            (source / "local-patch").touch()
            self.assertTrue((BUILD.extract(archive, container) / "local-patch").exists())
            shutil.rmtree(container)
            with mock.patch.object(tarfile.TarFile, "extractall", side_effect=OSError("interrupted")):
                with self.assertRaisesRegex(OSError, "interrupted"):
                    BUILD.extract(archive, container)
            self.assertFalse(container.exists())
            self.assertEqual((BUILD.extract(archive, container) / "source.c").read_bytes(), content)

    @unittest.skipUnless(shutil.which("git"), "Git required")
    def test_patching_resumes_after_an_interruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source.mkdir()
            (source / "first.c").write_text("after\n")  # First patch already applied.
            (source / "second.c").write_text("before\n")
            patches = []
            for name in ("first", "second"):
                patch = root / (name + ".patch")
                patch.write_text(f"--- a/{name}.c\n+++ b/{name}.c\n@@ -1 +1 @@\n-before\n+after\n")
                patches.append(patch)
            BUILD.apply_patches(source, patches)
            BUILD.apply_patches(source, patches)
            self.assertEqual((source / "second.c").read_text(), "after\n")

    @unittest.skipUnless(shutil.which("make"), "GNU Make required")
    def test_dependency_changes_relink_both_windows_targets(self):
        for arch in ("32", "64"):
            with self.subTest(arch=arch), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                quake = root / "Quake"
                quake.mkdir()
                windows = root / "Windows"
                windows.mkdir()
                (windows / "verify-static.py").touch()
                makefile = ROOT / f"Quake/Makefile.w{arch}"
                (quake / "ice").mkdir()
                for source in re.findall(r"\bice/\w+\.c\b", makefile.read_text()):
                    (quake / source).touch()
                for obj in set(re.findall(r"\b\w+\.o\b", makefile.read_text())):
                    (quake / obj).touch()
                (quake / "QuakeSpasm.res").touch()
                archive = root / "decoder.a"
                archive.touch()
                config = root / "qssm-static.mk"
                config.write_text(f"WINDOWS_STATIC_ARCHIVES := {archive.as_posix()}\n")
                exe = quake / "quakespasm.exe"
                exe.touch()
                # Explicit timestamps avoid filesystem timestamp granularity.
                for file in root.rglob("*"):
                    if file.is_file():
                        os.utime(file, (1000000000, 1000000000))
                os.utime(exe, (1000000001, 1000000001))
                command = ["make", "-q", "-f", str(makefile), "CC=true",
                           f"WINDOWS_STATIC_ROOT={root}", "quakespasm.exe"]
                def status():
                    result = subprocess.run(command, cwd=quake, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 2, result.stderr)
                    return result.returncode
                self.assertEqual(status(), 0)
                os.utime(archive, (1000000002, 1000000002))
                self.assertEqual(status(), 1, "a rebuilt archive must relink the executable")
                for option in ("VORBISLIB=tremor", "USE_CODEC_MODPLUG=1"):
                    result = subprocess.run([*command, option], cwd=quake, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("unsupported by static Windows builds", result.stderr)


@unittest.skipUnless(shutil.which("cc"), "C compiler required")
class UpdatePackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="qssm-static-update-")
        cls.work = Path(cls.tmp.name)
        source = (ROOT / "Quake/update.c").read_text()
        start = source.index("static qboolean M_Update_ValidateExtracted(")
        end = source.index("\nstatic const char *M_Update_PackageExecutableName", start)
        harness = r'''
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
typedef int qboolean;
#define true 1
#define false 0
#define q_snprintf snprintf
typedef struct { const char *platform; } updatereleaseinfo_t;
static int M_Update_RequireFile(const char *dir, const char *file, char *error, size_t size) {
    char path[4096]; struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
static int M_Update_FindMacOSBundleInExtract(const char *a, char *b, size_t c, char *d, size_t e) { return 0; }
'''
        harness += source[start:end]
        harness += r'''
int main(int argc, char **argv) {
    updatereleaseinfo_t info = { argv[1] }; char error[256];
    return M_Update_ValidateExtracted(&info, argv[2], error, sizeof(error)) ? 0 : 1;
}
'''
        (cls.work / "validator.c").write_text(harness)
        cls.exe = cls.work / "validator"
        subprocess.run(["cc", "-o", str(cls.exe), str(cls.work / "validator.c")], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_static_windows_packages_and_missing_required_files(self):
        for platform, executable in (("win64", "QSS-M-w64.exe"), ("win32", "QSS-M-w32.exe")):
            with tempfile.TemporaryDirectory(dir=self.work) as tmp:
                package = Path(tmp)
                names = (executable, "qssm.pak", "quakespasm.pak")
                for name in names:
                    (package / name).write_bytes(b"data")
                def validate():
                    return subprocess.run([str(self.exe), platform, tmp]).returncode
                self.assertEqual(validate(), 0)
                # Previous packages remain acceptable when DLLs are present.
                (package / "SDL3.dll").write_bytes(b"old runtime")
                (package / "libcurl.dll").write_bytes(b"old runtime")
                self.assertEqual(validate(), 0)
                for name in names:
                    (package / name).unlink()
                    self.assertEqual(validate(), 1, name)
                    (package / name).write_bytes(b"data")


if __name__ == "__main__":
    unittest.main()
