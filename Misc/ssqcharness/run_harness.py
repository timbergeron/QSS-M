#!/usr/bin/env python3
"""Build and run the QSS-M SSQC rotating-bmodel regression harness."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from build_test_bsp import build_bsp


HERE = Path(__file__).resolve().parent
SOURCE_DIR = HERE / "src"
ARTIFACTS_DIR = HERE / "artifacts"
FORBIDDEN = ("Program error", "Host_Error", "Sys_Error", "AddressSanitizer",
             "runtime error:", "Assertion failed", "unimplemented builtin")
ANSI_ESCAPE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|.)")
CASES = (
    {"id": 0, "name": "ordinary-accepted", "qex": False, "blocked": False,
     "late_warning": False},
    {"id": 1, "name": "ordinary-blocked", "qex": False, "blocked": True,
     "late_warning": True},
    {"id": 2, "name": "qex-blocked", "qex": True, "blocked": True,
     "late_warning": True},
    {"id": 3, "name": "qex-accepted", "qex": True, "blocked": False,
     "late_warning": False},
    {"id": 4, "name": "ordinary-unqueried", "qex": False, "blocked": False,
     "late_warning": False},
    {"id": 5, "name": "qex-enumeration", "qex": True, "blocked": False,
     "late_warning": False},
)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            value.update(chunk)
    return value.hexdigest()


def clean_output(output):
    output = ANSI_ESCAPE.sub("", output.replace("\r\n", "\n").replace("\r", "\n"))
    return "\n".join(line.rstrip() for line in output.splitlines()).rstrip() + "\n"


def find_base_pak(basedir):
    candidates = (basedir / "id1" / "pak0.pak", basedir / "id1" / "paks" / "pak0.pak")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("could not find id1/pak0.pak or id1/paks/pak0.pak")


def compile_progs(fteqcc, workspace, qex):
    source = workspace / ("qex-src" if qex else "ordinary-src")
    shutil.copytree(SOURCE_DIR, source)
    command = [str(fteqcc), "-Wall"]
    if qex:
        command.append("-DQEX=1")
    command.extend(("-srcfile", "progs.src"))
    process = subprocess.run(command, cwd=source, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
    output = process.stdout.decode("utf-8", "replace")
    log = ARTIFACTS_DIR / ("build-qex.log" if qex else "build-ordinary.log")
    log.write_text(" ".join(command) + "\n\n" + output)
    if process.returncode or re.search(r"\b(?:warning|error)\b", output, re.I):
        raise RuntimeError(f"QC compile failed; see {log}")
    result = source.parent / "progs.dat"
    if not result.is_file():
        raise RuntimeError(f"fteqcc did not produce {result}")
    final = workspace / ("progs-qex.dat" if qex else "progs-ordinary.dat")
    os.replace(result, final)
    return final


def save_config(path):
    return path.read_bytes() if path.exists() else None


def restore_config(path, saved):
    if saved is None:
        if path.exists():
            path.unlink()
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(saved)


def engine_command(binary, basedir, mod_name, case, mode):
    command = [str(binary), "-basedir", str(basedir), "-game", mod_name,
               "-nosound", "-condebug"]
    if mode == "dedicated":
        command.append("-dedicated")
    else:
        command.extend(("-window", "+listen", "1"))
    blocked = "0" if case["blocked"] else "1"
    command.extend((
        "+set", "developer", "1",
        "+set", "sys_ticrate", "0.02",
        "+set", "pr_checkextension", "1",
        "+set", "ssqc_case", str(case["id"]),
        "+set", "pr_ext_dp_sv_rotatingbmodel", blocked,
        "+set", "pr_ext_dp_sv_precacheanytime", blocked,
        "+set", "pr_ext_dp_ef_red", blocked,
        "+map", "rotating_pushers",
    ))
    return command


def run_case(binary, basedir, mod_dir, case, mode, progs):
    shutil.copyfile(progs, mod_dir / "progs.dat")
    qconsole = mod_dir / "qconsole.log"
    if qconsole.exists():
        qconsole.unlink()
    command = engine_command(binary, basedir, mod_dir.name, case, mode)
    started = time.monotonic()
    timed_out = False
    try:
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=30, env={**os.environ, "SDL_AUDIODRIVER": "dummy"})
        returncode = process.returncode
        output = clean_output(process.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired as error:
        returncode = None
        timed_out = True
        output = clean_output((error.stdout or b"").decode("utf-8", "replace"))
    if qconsole.exists():
        console_output = qconsole.read_text(errors="replace")
        if "SELFTEST DONE" not in output:
            output += "\n--- qconsole.log ---\n" + console_output
    duration = time.monotonic() - started
    log = ARTIFACTS_DIR / f"{mode}-{case['name']}.log"
    log.write_text(" ".join(command) + "\n\n" + output)

    passes = re.findall(r"^SELFTEST PASS (.*)$", output, re.M)
    failures = re.findall(r"^SELFTEST FAIL (.*)$", output, re.M)
    done = re.search(r"^SELFTEST DONE ssqc pass=(\d+) fail=(\d+)$", output, re.M)
    executable_stamp = re.search(r"^Exe: (.*)$", output, re.M)
    late_warning = "'DP_SV_PRECACHEANYTIME' not checked" in output
    errors = []
    if timed_out:
        errors.append("timeout")
    if returncode != 0:
        errors.append(f"exit status {returncode}")
    if not done:
        errors.append("missing completion marker")
    elif int(done.group(2)) != 0:
        errors.append(f"QC reported {done.group(2)} failures")
    if failures:
        errors.append("SELFTEST FAIL markers present")
    if not passes:
        errors.append("no pass markers")
    if not executable_stamp:
        errors.append("missing engine executable timestamp")
    if late_warning != case["late_warning"]:
        errors.append(f"late-precache warning was {late_warning}, expected {case['late_warning']}")
    for forbidden in FORBIDDEN:
        if forbidden in output:
            errors.append(f"forbidden output: {forbidden}")
    return {
        "case": case["name"],
        "mode": mode,
        "duration_seconds": round(duration, 3),
        "returncode": returncode,
        "timed_out": timed_out,
        "passes": passes,
        "failures": failures,
        "late_precache_warning": late_warning,
        "engine_exe_stamp": executable_stamp.group(1) if executable_stamp else None,
        "errors": errors,
        "log": str(log.relative_to(HERE)),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--basedir", type=Path, required=True)
    parser.add_argument("--fteqcc", type=Path, required=True)
    parser.add_argument("--server-mode", choices=("dedicated", "listen", "both"),
                        default="dedicated")
    args = parser.parse_args()
    binary = args.bin.resolve()
    basedir = args.basedir.expanduser().resolve()
    fteqcc = args.fteqcc.resolve()
    for path in (binary, basedir, fteqcc):
        if not path.exists():
            parser.error(f"missing: {path}")

    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    pak = find_base_pak(basedir)
    map_data = build_bsp()
    modes = ("dedicated", "listen") if args.server_mode == "both" else (args.server_mode,)
    id1_configs = (basedir / "id1" / "configs" / "config.cfg",
                   basedir / "id1" / "config.cfg")
    saved_configs = {path: save_config(path) for path in id1_configs}
    results = []
    configs_restored = False
    temporary_mod_removed = False

    with tempfile.TemporaryDirectory(prefix="qssm-ssqcharness-build-") as build_name:
        build_dir = Path(build_name)
        ordinary = compile_progs(fteqcc, build_dir, False)
        qex = compile_progs(fteqcc, build_dir, True)
        mod_name = f"ssqcharness-run-{os.getpid()}"
        mod_dir = basedir / mod_name
        if mod_dir.exists():
            raise RuntimeError(f"refusing to reuse existing directory {mod_dir}")
        try:
            (mod_dir / "maps").mkdir(parents=True)
            (mod_dir / "maps" / "rotating_pushers.bsp").write_bytes(map_data)
            for mode in modes:
                for case in CASES:
                    result = run_case(binary, basedir, mod_dir, case, mode,
                                      qex if case["qex"] else ordinary)
                    results.append(result)
                    status = "PASS" if not result["errors"] else "FAIL"
                    print(f"{status} {mode}/{case['name']} ({result['duration_seconds']}s)")
                    for error in result["errors"]:
                        print(f"  {error}")
        finally:
            for path, saved in saved_configs.items():
                restore_config(path, saved)
            if mod_dir.exists():
                shutil.rmtree(mod_dir)
            configs_restored = all(save_config(path) == saved
                                   for path, saved in saved_configs.items())
            temporary_mod_removed = not mod_dir.exists()

        source_files = (HERE / "build_test_bsp.py", HERE / "run_harness.py",
                        SOURCE_DIR / "defs.qc", SOURCE_DIR / "world.qc",
                        SOURCE_DIR / "progs.src")
        summary = {
            "engine": {"path": str(binary), "sha256": digest(binary),
                       "mtime": binary.stat().st_mtime},
            "qc_compiler": {"path": str(fteqcc), "sha256": digest(fteqcc),
                            "mtime": fteqcc.stat().st_mtime},
            "base_pak": {"path": str(pak), "sha256": digest(pak)},
            "map": {"sha256": hashlib.sha256(map_data).hexdigest(),
                    "bytes": len(map_data),
                    "builder": "build_test_bsp.py"},
            "progs": {
                "ordinary_sha256": digest(ordinary),
                "qex_sha256": digest(qex),
            },
            "sources": {str(path.relative_to(HERE)): digest(path)
                        for path in source_files},
            "cleanup": {"configs_restored": configs_restored,
                        "temporary_mod_removed": temporary_mod_removed},
            "results": results,
            "passed": (configs_restored and temporary_mod_removed and
                       all(not result["errors"] for result in results)),
        }
        (ARTIFACTS_DIR / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    raise SystemExit(0 if summary["passed"] else 1)


if __name__ == "__main__":
    main()
