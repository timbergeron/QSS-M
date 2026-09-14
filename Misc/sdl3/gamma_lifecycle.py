#!/usr/bin/env python3
"""macOS hardware-gamma lifecycle check against the real display transfer table.

Usage: python3 Misc/sdl3/gamma_lifecycle.py <label> <path/to/QSS-M.app>
           [--no-hwgamma] [--cfg "vid_fsaa 4"]... [--settle SECONDS]

Launches the app with gamma 0.6 in a scratch basedir (with -hwgamma unless
--no-hwgamma), then reads the main display's table after launch, with another
app focused, after refocusing, after vid_restart and after quit. --cfg lines go
into the scratch config so video cvars such as vid_fsaa or vid_fullscreen apply
at startup; --settle adds time for slower fullscreen transitions. Uses
localhost rcon only and always kills the game at the end. Set QSSM_PAKS to a
directory holding pak0.pak/pak1.pak. The run changes the screen's gamma for
about 30 seconds.
"""
import argparse, os, shutil, signal, socket, struct, subprocess, tempfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PAKS = Path(os.environ.get("QSSM_PAKS", Path.home() / "Library/Application Support/QuakeSpasm/id1"))
PORT, PASSWORD = 26173, "gammatest"

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("label")
ap.add_argument("app")
ap.add_argument("--no-hwgamma", action="store_true", help="expect the default gamma selection")
ap.add_argument("--cfg", action="append", default=[], help="config line applied at startup")
ap.add_argument("--settle", type=float, default=0.0, help="extra seconds after each transition")
args = ap.parse_args()

label, app = args.label, Path(args.app).resolve()
work = Path(tempfile.gettempdir()) / "qssm-gamma-lifecycle"
work.mkdir(exist_ok=True)
base = work / f"base-{label}"
cgtable = work / "cgtable"
subprocess.run(["cc", "-O2", "-framework", "ApplicationServices", "-o", str(cgtable),
                str(HERE / "cgtable.c")], check=True)


def table(tag):
    out = work / f"{label}-{tag}.txt"
    subprocess.run([str(cgtable), str(out)], check=True, capture_output=True)
    rows, main = [], False
    for line in out.read_text().splitlines():
        if line.startswith("display"):
            main = " main=1 " in line
        elif main:
            rows.append(tuple(float(v) for v in line.split()))
    return rows


def same(a, b, tol=2e-4):
    return len(a) == len(b) and all(abs(x - y) <= tol for ra, rb in zip(a, b) for x, y in zip(ra, rb))


def rcon(cmd):
    body = b"\x05" + PASSWORD.encode() + b"\x00" + cmd.encode() + b"\x00"
    pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(pkt, ("127.0.0.1", PORT))


def pids():
    r = subprocess.run(["pgrep", "-f", str(base)], capture_output=True, text=True)
    return [int(p) for p in r.stdout.split()]


system = table("system")
shutil.rmtree(base, ignore_errors=True)
(base / "id1" / "configs").mkdir(parents=True)
for pak in ("pak0.pak", "pak1.pak"):
    (base / "id1" / pak).symlink_to(PAKS / pak)
if args.cfg:
    config = "".join(line + "\n" for line in args.cfg)
    (base / "id1" / "config.cfg").write_text(config)             # read at startup either way
    (base / "id1" / "configs" / "config.cfg").write_text(config)

results = []
def step(name, expect_ramp, wait):
    time.sleep(wait + args.settle)
    t = table(name.replace(" ", "_"))
    applied = not same(t, system)
    mid = t[len(t) // 2][0] if t else float("nan")
    ok = applied == expect_ramp
    results.append(ok)
    print(f"  {name:<22} expect={'ramp' if expect_ramp else 'system'} "
          f"got={'ramp' if applied else 'system'} mid={mid:.4f} {'OK' if ok else 'FAIL'}", flush=True)

launch = ["open", "-n", "-a", str(app), "--stdout", str(base / "stdout.txt"),
          "--stderr", str(base / "stderr.txt"), "--args",
          "-basedir", str(base), "-noquakeimport", "-port", str(PORT), "+rcon_password", PASSWORD,
          "+gamma", "0.6", "+listen", "1", "+map", "start"]
if not args.cfg:
    launch[launch.index("--args") + 1:launch.index("--args") + 1] = ["-window", "-width", "640", "-height", "480"]
if not args.no_hwgamma:
    launch.insert(launch.index("--args") + 1, "-hwgamma")

try:
    subprocess.run(launch, check=True)
    print(f"{label}: system mid={system[len(system) // 2][0]:.4f} cfg={args.cfg} hwgamma={not args.no_hwgamma}")
    step("after launch", True, 10)
    subprocess.run(["open", "-a", "Finder"], check=True)
    step("other app focused", False, 3)
    subprocess.run(["open", "-a", str(app)], check=True)
    step("game refocused", True, 4)
    rcon("vid_restart")
    step("after vid_restart", True, 7)
    rcon("quit")
    deadline = time.time() + 15
    while pids() and time.time() < deadline:
        time.sleep(0.3)
    print(f"  exited cleanly: {not pids()}")
    step("after quit", False, 1)
finally:
    for p in pids():
        os.kill(p, signal.SIGKILL)
    time.sleep(1)
    print(f"  desktop table restored at end: {same(table('final'), system)}")
    log = base / "stdout.txt"
    if log.exists():
        for line in log.read_text(errors="replace").splitlines():
            if line.startswith(("Video mode", "Enabled: GLSL gamma")) or "hardware gamma" in line:
                print("  log:", line)
print(f"{label}: {'PASS' if all(results) else 'FAIL'} ({sum(results)}/{len(results)})")
raise SystemExit(0 if all(results) else 1)
