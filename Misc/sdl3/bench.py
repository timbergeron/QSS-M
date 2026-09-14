#!/usr/bin/env python3
"""Interleaved A/B benchmark for the SDL migration baselines.

Usage: python3 Misc/sdl3/bench.py <out.json> label=/path/QSS-M.app [label=...]

For each app, alternating between them: timedemo demo1 in a focused
1280x800 window, then warm -window +quit launches with and without sound.
Launch-to-ready is timed to "Quake Initialized" on stdout; the sound minus
-nosound difference isolates CoreAudio init and shutdown. Scratch basedirs
only; set QSSM_PAKS to a directory holding pak0.pak/pak1.pak.
"""
import json, os, re, statistics, subprocess, sys, tempfile, time
from pathlib import Path

PAKS = Path(os.environ.get("QSSM_PAKS", Path.home() / "Library/Application Support/QuakeSpasm/id1"))
RUNS = int(os.environ.get("QSSM_BENCH_RUNS", "5"))
FPS = re.compile(r"(\d+) frames\s+([\d.]+) seconds\s+([\d.]+) fps")
WORK = Path(tempfile.gettempdir()) / "qssm-sdl3-bench"

out_path = Path(sys.argv[1])
apps = {label: str(Path(path).resolve()) for label, path in (arg.split("=", 1) for arg in sys.argv[2:])}


def basedir(label):
    b = WORK / f"base-{label}"
    if not b.exists():
        (b / "id1").mkdir(parents=True)
        for pak in ("pak0.pak", "pak1.pak"):
            (b / "id1" / pak).symlink_to(PAKS / pak)
    return b


def kill(b):
    subprocess.run(["pkill", "-9", "-f", str(b)], capture_output=True)
    time.sleep(1.0)


def timedemo(label, app):
    b = basedir(label)
    log = Path(app).parent / "qconsole.log"   # -condebug writes next to the app
    log.unlink(missing_ok=True)
    subprocess.run(["open", "-n", "-a", app, "--args", "-basedir", str(b), "-noquakeimport",
                    "-window", "-width", "1280", "-height", "800", "-condebug",
                    "+timedemo", "demo1"], check=True)
    deadline = time.time() + 90
    try:
        while time.time() < deadline:
            if log.exists():
                m = FPS.search(log.read_text(errors="replace"))
                if m:
                    return float(m.group(3))
            time.sleep(0.25)
        return None
    finally:
        kill(b)


def launch_quit(label, app, nosound):
    b = basedir(label)
    args = [str(Path(app) / "Contents/MacOS/QSS-M"), "-basedir", str(b), "-noquakeimport",
            "-window", "-width", "640", "-height", "480"]
    if nosound:
        args.append("-nosound")
    args.append("+quit")
    start = time.monotonic()
    ready = None
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    for line in proc.stdout:
        if ready is None and "Quake Initialized" in line:
            ready = time.monotonic() - start
    proc.wait(timeout=60)
    total = time.monotonic() - start
    return {"ready": ready, "quit": None if ready is None else total - ready, "total": total}


def med(values):
    values = [v for v in values if v is not None]
    return round(statistics.median(values), 4) if values else None


results = {label: {"fps": [], "sound": [], "nosound": []} for label in apps}
for label, app in apps.items():          # warm-up: page in binaries and paks
    launch_quit(label, app, False)
for run in range(RUNS):
    for label, app in apps.items():      # interleave to spread thermal and background drift
        results[label]["fps"].append(timedemo(label, app))
        results[label]["sound"].append(launch_quit(label, app, False))
        results[label]["nosound"].append(launch_quit(label, app, True))
        print(f"run {run + 1} {label}: fps={results[label]['fps'][-1]}", flush=True)

summary = {}
for label, r in results.items():
    s = {
        "timedemo_fps_median": med(r["fps"]),
        "timedemo_fps_runs": r["fps"],
        "launch_to_ready_s": med([x["ready"] for x in r["sound"]]),
        "quit_to_exit_s": med([x["quit"] for x in r["sound"]]),
        "launch_to_ready_nosound_s": med([x["ready"] for x in r["nosound"]]),
        "quit_to_exit_nosound_s": med([x["quit"] for x in r["nosound"]]),
    }
    if None not in (s["launch_to_ready_s"], s["launch_to_ready_nosound_s"],
                    s["quit_to_exit_s"], s["quit_to_exit_nosound_s"]):
        s["audio_init_s"] = round(s["launch_to_ready_s"] - s["launch_to_ready_nosound_s"], 4)
        s["audio_shutdown_s"] = round(s["quit_to_exit_s"] - s["quit_to_exit_nosound_s"], 4)
    summary[label] = s
out_path.write_text(json.dumps({"summary": summary, "raw": results}, indent=2))
print(json.dumps(summary, indent=2))
