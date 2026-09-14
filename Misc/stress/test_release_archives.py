"""Run the release scripts on staging fixtures and inspect their actual ZIPs.

Compilation and Git metadata use test doubles; copying, shell control flow and
zip are real. Reusing an old ZIP must not retain SDL2 or an omitted SDL3 runtime.
No toolchain, engine build, or writes to the source checkout are required.
"""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if executable:
        path.chmod(0o755)


class ReleaseArchiveTests(unittest.TestCase):
    def run_package(self, target, runtime=False):
        with tempfile.TemporaryDirectory(prefix="qssm-package-test-") as tmp:
            root = Path(tmp)
            script = {"w32": "build-w32.sh", "w64": "build-w64.sh", "l64": "build-linux.sh"}[target]
            shutil.copy2(ROOT / script, root / script)
            shutil.copy2(ROOT / "ci-version.sh", root / "ci-version.sh")
            for name in ("LICENSE.txt", "Quakespasm.html", "Quakespasm.txt",
                         "Quakespasm-Spiked.txt", "Quakespasm-Music.txt"):
                write(root / name, "current documentation\n")
            for name in ("quakespasm.pak", "qssm.pak", "gamecontrollerdb.txt"):
                write(root / "Quake" / name, "current data\n")
            write(root / "bin/make", "#!/bin/sh\nfor arg do [ \"$arg\" != clean ] || exit 0; done\nprintf 'fresh engine\\n' > quakespasm\n", True)
            write(root / "bin/git", "#!/bin/sh\ncase \"$*\" in\n*--format=%ct*) echo 1700000000;;\n*--format=%cd*) echo 2026-09-14;;\n*) echo 0123456789012345678901234567890123456789;;\nesac\n", True)
            if target != "l64":
                bits = "32" if target == "w32" else "64"
                arch = "x86" if target == "w32" else "x64"
                write(root / f"Quake/build_cross_win{bits}.sh", "#!/bin/sh\nprintf 'fresh engine\\n' > quakespasm.exe\n", True)
                for name in (f"Windows/codecs/{arch}/libogg-0.dll",
                             f"Windows/curl/lib/{arch}/libcurl.dll",
                             f"Windows/zlib/{arch}/zlib1.dll",
                             f"Windows/SDL3/lib/{arch}/SDL3.dll"):
                    write(root / name, "current runtime\n")
            exe = f"QSS-M-{target}" + (".exe" if target != "l64" else "")
            archive = root / "Quake" / f"QSS-M-{target}.zip"
            with zipfile.ZipFile(archive, "w") as old:
                old.writestr(exe, "old engine")
                old.writestr("SDL2.dll", "obsolete SDL2")
                old.writestr("libSDL3.so.0", "old optional runtime")
                old.writestr("retired-codec.dll", "obsolete codec")
            env = os.environ.copy()
            env["PATH"] = str(root / "bin") + os.pathsep + env["PATH"]
            env["TARGET_TRIPLET"] = "qssm-fixture-no-toolchain"
            env.pop("SDL3_RUNTIME", None)
            env.pop("CROSS_GNUTLS_DLL_DIRS", None)
            if runtime:
                library = root / "runtime/libSDL3.so.0"
                write(library, "current SDL3\n")
                env["SDL3_RUNTIME"] = str(library)
            result = subprocess.run(["sh", str(root / script)], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with zipfile.ZipFile(archive) as package:
                self.assertEqual(package.read(exe), b"fresh engine\n")
                self.assertNotIn("SDL2.dll", package.namelist())
                self.assertNotIn("retired-codec.dll", package.namelist())
                if target != "l64":
                    self.assertEqual(package.read("SDL3.dll"), b"current runtime\n")
                if runtime:
                    self.assertEqual(package.read("libSDL3.so.0"), b"current SDL3\n")
                else:
                    self.assertNotIn("libSDL3.so.0", package.namelist())

    def test_windows_32_rebuild_removes_retired_runtime(self):
        self.run_package("w32")

    def test_windows_64_rebuild_removes_retired_runtime(self):
        self.run_package("w64")

    def test_linux_system_rebuild_removes_previously_bundled_runtime(self):
        self.run_package("l64")

    def test_linux_bundled_rebuild_includes_selected_runtime(self):
        self.run_package("l64", runtime=True)


if __name__ == "__main__":
    unittest.main()
