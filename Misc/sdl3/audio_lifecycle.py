#!/usr/bin/env python3
"""Sound lifecycle check driven through snd_restart.

Usage: python3 Misc/sdl3/audio_lifecycle.py <label> <path/to/QSS-M.app>

Launches the app windowed with sound in a scratch basedir, then over localhost
rcon restarts sound with surround off, 8-bit samples, several mix rates and the
defaults, and takes focus away and back. Each step reports the backend's device
lines and checks that sound restarted with the requested bits and rate and
that the DMA cursor keeps advancing, which only happens while the audio callback
is consuming the ring. Set QSSM_PAKS to a directory holding pak0.pak/pak1.pak.
Plays whatever the start map makes audible and always kills the game at the end.
"""
import argparse, os, re, shutil, signal, socket, struct, subprocess, tempfile, time
from pathlib import Path

PAKS = Path(os.environ.get("QSSM_PAKS", Path.home() / "Library/Application Support/QuakeSpasm/id1"))
PORT, PASSWORD = 26175, "sndtest"
FORMAT = re.compile(r"^(\d+) bit, (stereo|mono), (\d+) Hz$")
COUNT = re.compile(r"^\s*(-?\d+) (samples|samplepos)$")
BACKEND = re.compile(r"^(SDL audio (spec|device|driver)|Couldn't|Failed|Shutting down SDL sound|sound system not started)")

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("label")
ap.add_argument("app")
args = ap.parse_args()

app = Path(args.app).resolve()
base = Path(tempfile.gettempdir()) / "qssm-audio-lifecycle" / f"base-{args.label}"
stdout = base / "stdout.txt"   # flushed after every print


def rcon(cmd):
    body = b"\x05" + PASSWORD.encode() + b"\x00" + cmd.encode() + b"\x00"
    pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(pkt, ("127.0.0.1", PORT))


def batch(*cmds):
    """rcon executes one command per packet; an alias runs several in one command buffer pass."""
    rcon('alias qssm_snd_batch "' + "; ".join(cmds) + '"')
    time.sleep(0.3)
    rcon("qssm_snd_batch")


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


def soundinfo(text, marker):
    """(bits, rate, samples, samplepos) printed after an echoed marker, or None."""
    lines = [line.strip() for line in text.splitlines()]
    if marker not in lines:
        return None
    info = {}
    for line in lines[lines.index(marker) + 1:]:
        if line.startswith("qssm-snd-"):
            break
        if line == "sound system not started":
            return None
        m = FORMAT.match(line)
        if m:
            info["bits"], info["rate"] = int(m.group(1)), int(m.group(3))
        m = COUNT.match(line)
        if m:
            info[m.group(2)] = int(m.group(1))
    keys = ("bits", "rate", "samples", "samplepos")
    return tuple(info[k] for k in keys) if all(k in info for k in keys) else None


console = Console()
results = []


def probe(tag, reads=3):
    """Read soundinfo several times; return the readings and whether the cursor moved."""
    console.new()
    readings = []
    for i in range(reads):
        batch(f"echo qssm-snd-{tag}-{i}", "soundinfo")
        time.sleep(0.45)
    text = console.new()
    for i in range(reads):
        readings.append(soundinfo(text, f"qssm-snd-{tag}-{i}"))
    moving = all(readings) and len({r[3] for r in readings}) > 1
    return readings, moving, text


def report(name, readings, moving, text, bits=None, rate=None):
    first = readings[0] if readings else None
    ok = bool(first) and moving and (bits is None or first[0] == bits) and (rate is None or first[1] == rate)
    results.append(ok)
    shown = f"{first[0]}-bit {first[1]} Hz, {first[2]} samples" if first else "no sound"
    cursor = [r[3] for r in readings if r]
    print(f"  {name:<18} {shown:<30} cursor={cursor} {'OK' if ok else 'FAIL'}", flush=True)
    for line in text.splitlines():
        if BACKEND.match(line.strip()):
            print("    log:", line.strip())


shutil.rmtree(base, ignore_errors=True)
(base / "id1").mkdir(parents=True)
for pak in ("pak0.pak", "pak1.pak"):
    (base / "id1" / pak).symlink_to(PAKS / pak)

launch = ["open", "-n", "-a", str(app), "--stdout", str(stdout), "--stderr", str(base / "stderr.txt"), "--args",
          "-basedir", str(base), "-noquakeimport", "-port", str(PORT), "-window", "-width", "640", "-height", "480",
          "+rcon_password", PASSWORD, "+listen", "1", "+map", "start"]
try:
    subprocess.run(launch, check=True)
    time.sleep(10)
    startup = console.new()
    readings, moving, text = probe("start")
    report("startup", readings, moving, startup + text, 16, 44100)

    steps = [
        ("surround off", ["snd_surround 0"], 16, 44100),
        ("8-bit", ["loadas8bit 1"], 8, 44100),
        ("22050 Hz", ["loadas8bit 0", "snd_mixspeed 22050"], 16, 22050),
        ("11025 Hz", ["snd_mixspeed 11025"], 16, 11025),
        ("48000 Hz", ["snd_mixspeed 48000"], 16, 48000),
        ("96000 Hz", ["snd_mixspeed 96000"], 16, 96000),
        ("defaults", ["snd_mixspeed 44100", "snd_surround 1"], 16, 44100),
    ]
    for i, (name, cvars, bits, rate) in enumerate(steps):
        console.new()
        batch(*cvars, "snd_restart")
        time.sleep(1.5)
        restart = console.new()
        readings, moving, text = probe(f"step{i}")
        report(name, readings, moving, restart + text, bits, rate)

    subprocess.run(["open", "-a", "Finder"], check=True)
    time.sleep(2)
    readings, moving, text = probe("unfocused")
    report("other app focused", readings, moving, text, 16, 44100)
    subprocess.run(["open", "-a", str(app)], check=True)
    time.sleep(2)
    readings, moving, text = probe("refocused")
    report("refocused", readings, moving, text, 16, 44100)

    console.new()
    rcon("quit")
    deadline = time.time() + 15
    while pids() and time.time() < deadline:
        time.sleep(0.3)
    exited = not pids()
    results.append(exited)
    print(f"  exited cleanly: {exited}")
    for line in console.new().splitlines():
        if BACKEND.match(line.strip()):
            print("    log:", line.strip())
finally:
    for p in pids():
        os.kill(p, signal.SIGKILL)

print(f"{args.label}: {'PASS' if results and all(results) else 'FAIL'} ({sum(results)}/{len(results)})")
raise SystemExit(0 if results and all(results) else 1)
