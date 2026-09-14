#!/usr/bin/env python3
"""Video mode transition check driven through vid_restart.

Usage: python3 Misc/sdl3/vid_transitions.py <label> <path/to/QSS-M.app> [--settle SECONDS] [--cycles N]

Launches the app windowed in a scratch basedir, records its mode
list from stdout, then changes modes over localhost rcon: desktop fullscreen, windowed,
exclusive fullscreen, windowed, desktop fullscreen again and windowed again.
After every vid_restart it compares the mode the engine describes immediately
with the mode it describes once the window has settled, and prints any
fullscreen warnings the transition logged. SDL2 finished each window change
before returning, so both reports should agree. The reports come from
vid_describecurrentmode, which reads SDL's live window size and fullscreen flag
rather than the vid cvars. --cycles repeats the six transitions and summarizes
completed, refused and failed transitions. Set QSSM_PAKS to a directory
holding pak0.pak/pak1.pak. The run takes the screen fullscreen several times
over about a minute and always kills the game at the end.
"""
import argparse, os, re, shutil, signal, socket, struct, subprocess, tempfile, time
from pathlib import Path

PAKS = Path(os.environ.get("QSSM_PAKS", Path.home() / "Library/Application Support/QuakeSpasm/id1"))
PORT, PASSWORD = 26174, "vidtest"
MODE = re.compile(r"^(\d+)x(\d+)x(\d+) (\d+)Hz (fullscreen|windowed)$")
LISTED = re.compile(r"\b(\d{3,4}) *x *(\d{3,4}) *x *(\d{2})\b")   # vid_describemodes: "1280 x  800 x 32 : 120"
NOTE = re.compile(r"fullscreen|windowed mode|couldn't|falling back|keeping|video mode", re.I)
REFUSED = re.compile(r"couldn't (enter|prepare|leave)|falling back|keeping the current video mode|reported (failure|success)", re.I)

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("label")
ap.add_argument("app")
ap.add_argument("--settle", type=float, default=4.0, help="seconds to wait after each vid_restart")
ap.add_argument("--cycles", type=int, default=1, help="times to repeat the six transitions")
args = ap.parse_args()

app = Path(args.app).resolve()
base = Path(tempfile.gettempdir()) / "qssm-vid-transitions" / f"base-{args.label}"
stdout = base / "stdout.txt"   # flushed after every print; the -condebug log lags and loses its tail on kill


def rcon(cmd):
    body = b"\x05" + PASSWORD.encode() + b"\x00" + cmd.encode() + b"\x00"
    pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(pkt, ("127.0.0.1", PORT))


def batch(*cmds):
    """rcon executes one command per packet; an alias runs several in one command buffer pass."""
    rcon('alias qssm_vid_batch "' + "; ".join(cmds) + '"')
    time.sleep(0.3)
    rcon("qssm_vid_batch")


def pids():
    r = subprocess.run(["pgrep", "-f", str(base)], capture_output=True, text=True)
    return [int(p) for p in r.stdout.split()]


class Console:
    def __init__(self):
        self.pos = 0

    def new(self):
        if not stdout.exists():
            return ""
        data = stdout.read_bytes()
        text = data[self.pos:].decode(errors="replace")
        self.pos = len(data)
        return text


def section(text, marker):
    """Lines printed after an echoed marker, up to the next marker."""
    lines = [line.strip() for line in text.splitlines()]   # echo leaves a trailing space
    try:
        start = lines.index(marker) + 1
    except ValueError:
        return []
    end = next((i for i in range(start, len(lines)) if lines[i].startswith("qssm-vid-")), len(lines))
    return lines[start:end]


def described(text, marker):
    for line in section(text, marker):
        m = MODE.match(line.strip())
        if m:
            return int(m.group(1)), int(m.group(2)), m.group(5)
    return None


def matches(mode, state, size):
    return mode is not None and mode[2] == state and (size is None or mode[:2] == size)


shutil.rmtree(base, ignore_errors=True)
(base / "id1").mkdir(parents=True)
for pak in ("pak0.pak", "pak1.pak"):
    (base / "id1" / pak).symlink_to(PAKS / pak)

console = Console()
launch = ["open", "-n", "-a", str(app), "--stdout", str(stdout),
          "--stderr", str(base / "stderr.txt"), "--args",
          "-basedir", str(base), "-noquakeimport", "-port", str(PORT),
          "-window", "-width", "640", "-height", "480",
          "+rcon_password", PASSWORD, "+listen", "1", "+map", "start"]
results = []
refusals = 0
try:
    subprocess.run(launch, check=True)
    time.sleep(10)
    console.new()
    batch("echo qssm-vid-modes", "vid_describemodes", "echo qssm-vid-start", "vid_describecurrentmode")
    time.sleep(1.5)
    text = console.new()
    modes = sorted({(int(w), int(h)) for line in section(text, "qssm-vid-modes")
                    for w, h, _ in LISTED.findall(line)}, reverse=True)
    print(f"{args.label}: {len(modes)} modes: {' '.join(f'{w}x{h}' for w, h in modes)}")
    print(f"  start: {described(text, 'qssm-vid-start')}")
    if not modes:
        raise SystemExit("no video modes were listed")
    exclusive = modes[len(modes) // 2]

    steps = [
        ("desktop fullscreen", "vid_desktopfullscreen 1; vid_fullscreen 1; vid_restart", "fullscreen", None),
        ("windowed 800x600", "vid_fullscreen 0; vid_width 800; vid_height 600; vid_restart", "windowed", (800, 600)),
        (f"exclusive {exclusive[0]}x{exclusive[1]}",
         f"vid_desktopfullscreen 0; vid_fullscreen 1; vid_width {exclusive[0]}; vid_height {exclusive[1]}; vid_restart",
         "fullscreen", exclusive),
        ("windowed 640x480", "vid_fullscreen 0; vid_width 640; vid_height 480; vid_restart", "windowed", (640, 480)),
        ("desktop fullscreen again", "vid_desktopfullscreen 1; vid_fullscreen 1; vid_restart", "fullscreen", None),
        ("windowed 1024x640", "vid_fullscreen 0; vid_width 1024; vid_height 640; vid_restart", "windowed", (1024, 640)),
    ]
    for i, (cycle, (name, cmd, state, size)) in enumerate((c, step) for c in range(args.cycles) for step in steps):
        console.new()
        started = time.time()
        batch(*cmd.split("; "), f"echo qssm-vid-{i}-immediate", "vid_describecurrentmode")
        time.sleep(args.settle)
        batch(f"echo qssm-vid-{i}-settled", "vid_describecurrentmode")
        time.sleep(1.5)
        text = console.new()
        immediate = described(text, f"qssm-vid-{i}-immediate")
        settled = described(text, f"qssm-vid-{i}-settled")
        notes = [line.strip() for line in text.splitlines()
                 if NOTE.search(line) and not line.startswith("qssm-vid-") and not MODE.match(line.strip())]
        refused = any(REFUSED.search(line) for line in notes)
        # A refused request only counts when the engine settled on a valid mode it describes consistently.
        ok = immediate == settled and (matches(settled, state, size) or (refused and settled is not None))
        refusals += refused and ok
        results.append(ok)
        label = "OK" if ok and not refused else "REFUSED, recovered" if ok else "FAIL"
        if args.cycles == 1 or not ok or refused:
            prefix = f"cycle {cycle + 1} " if args.cycles > 1 else ""
            print(f"  {prefix}{name:<26} immediate={immediate} settled={settled} "
                  f"{time.time() - started:.1f}s {label}", flush=True)
            for line in notes:
                print("    log:", line)
        elif (i + 1) % len(steps) == 0:
            print(f"  cycle {cycle + 1}/{args.cycles}: {sum(results)}/{len(results)} transitions confirmed", flush=True)
        if not pids():
            print("  the game exited during the run")
            break
    rcon("quit")
    deadline = time.time() + 15
    while pids() and time.time() < deadline:
        time.sleep(0.3)
    print(f"  exited cleanly: {not pids()}")
finally:
    for p in pids():
        os.kill(p, signal.SIGKILL)

expected = args.cycles * 6
print(f"{args.label}: {'PASS' if len(results) == expected and all(results) else 'FAIL'} "
      f"({sum(results)}/{expected} confirmed, {refusals} refused with recovery, {len(results) - sum(results)} failed)")
raise SystemExit(0 if len(results) == expected and all(results) else 1)
