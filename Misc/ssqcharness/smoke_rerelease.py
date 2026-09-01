#!/usr/bin/env python3
"""Smoke-load every shipped MG1/MG3 map containing a continuous rotator."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
ARTIFACTS_DIR = HERE / "artifacts"
PACK_MAPS = {
    "mg1": ("horde1", "horde3", "horde6", "horde7", "hub", "mge1m1",
            "mge1m2", "mge1m3", "mge2m1", "mge2m2", "mge4m1", "mge5m2",
            "start"),
    "mg3": ("map1", "map5", "map6"),
}
FORBIDDEN = ("Host_Error", "Sys_Error", "Program error", "Assertion failed",
             "AddressSanitizer", "runtime error:")
ANSI_ESCAPE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|.)")


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            value.update(chunk)
    return value.hexdigest()


def clean_output(output):
    output = ANSI_ESCAPE.sub("", output.replace("\r\n", "\n").replace("\r", "\n"))
    return "\n".join(line.rstrip() for line in output.splitlines()).rstrip() + "\n"


def find_pak(directory):
    for candidate in (directory / "pak0.pak", directory / "paks" / "pak0.pak"):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"missing pak0.pak in {directory}")


def save(path):
    return path.read_bytes() if path.exists() else None


def restore(path, value):
    if value is None:
        if path.exists():
            path.unlink()
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--basedir", type=Path, required=True)
    args = parser.parse_args()
    binary = args.bin.resolve()
    basedir = args.basedir.expanduser().resolve()
    if not binary.is_file():
        parser.error(f"missing executable: {binary}")
    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)

    configs = []
    packs = {}
    for game in PACK_MAPS:
        game_dir = basedir / game
        packs[game] = find_pak(game_dir)
        configs.extend((game_dir / "config.cfg", game_dir / "configs" / "config.cfg"))
    saved = {path: save(path) for path in configs}
    results = []
    try:
        for game, maps in PACK_MAPS.items():
            for map_name in maps:
                command = [str(binary), "-basedir", str(basedir), "-game", game,
                           "-nosound", "-dedicated", "+set", "developer", "1",
                           "+map", map_name, "+wait", "+wait", "+quit"]
                started = time.monotonic()
                timed_out = False
                try:
                    process = subprocess.run(
                        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=45, env={**os.environ, "SDL_AUDIODRIVER": "dummy"})
                    returncode = process.returncode
                    output = clean_output(process.stdout.decode("utf-8", "replace"))
                except subprocess.TimeoutExpired as error:
                    returncode = None
                    timed_out = True
                    output = clean_output((error.stdout or b"").decode("utf-8", "replace"))
                log = ARTIFACTS_DIR / f"smoke-{game}-{map_name}.log"
                log.write_text(" ".join(command) + "\n\n" + output)
                errors = []
                if timed_out:
                    errors.append("timeout")
                if returncode != 0:
                    errors.append(f"exit status {returncode}")
                if f"SpawnServer: {map_name}" not in output:
                    errors.append("missing SpawnServer marker")
                if "Server spawned." not in output:
                    errors.append("missing server-spawned marker")
                for forbidden in FORBIDDEN:
                    if forbidden in output:
                        errors.append(f"forbidden output: {forbidden}")
                stamp = re.search(r"^Exe: (.*)$", output, re.M)
                if not stamp:
                    errors.append("missing engine executable timestamp")
                result = {
                    "game": game,
                    "map": map_name,
                    "duration_seconds": round(time.monotonic() - started, 3),
                    "returncode": returncode,
                    "timed_out": timed_out,
                    "engine_exe_stamp": stamp.group(1) if stamp else None,
                    "errors": errors,
                    "log": str(log.relative_to(HERE)),
                }
                results.append(result)
                print(f"{'PASS' if not errors else 'FAIL'} {game}/{map_name}")
                for error in errors:
                    print(f"  {error}")
    finally:
        for path, value in saved.items():
            restore(path, value)

    configs_restored = all(save(path) == value for path, value in saved.items())
    summary = {
        "engine": {"path": str(binary), "sha256": digest(binary),
                   "mtime": binary.stat().st_mtime},
        "packs": {game: {"path": str(path), "sha256": digest(path)}
                  for game, path in packs.items()},
        "configs_restored": configs_restored,
        "results": results,
        "passed": configs_restored and all(not result["errors"] for result in results),
    }
    (ARTIFACTS_DIR / "smoke-summary.json").write_text(
        json.dumps(summary, indent=2) + "\n")
    raise SystemExit(0 if summary["passed"] else 1)


if __name__ == "__main__":
    main()
