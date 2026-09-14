#!/usr/bin/env python3
"""Engine workload check for the SDL3 migration.

Usage: python3 Misc/sdl3/engine_workloads.py <label> <path/to/QSS-M.app>

In a scratch basedir: a windowed listen server records a demo, saves and loads
a game, takes a screenshot, changes map and quits over localhost rcon; a second
launch plays the recorded demo back from the command line; a dedicated server
starts, serves a map and exits on SIGTERM. Every step is judged from the files
it leaves and from the engine's console, not from the command having been sent.
Set QSSM_PAKS to a directory holding pak0.pak/pak1.pak. Always kills its own
processes at the end.
"""
import argparse, os, shutil, signal, socket, struct, subprocess, tempfile, time
from pathlib import Path

PAKS = Path(os.environ.get("QSSM_PAKS", Path.home() / "Library/Application Support/QuakeSpasm/id1"))
PORT, PASSWORD = 26176, "worktest"
ERRORS = ("QUAKE ERROR", "Host_Error", "SZ_GetSpace", "Hunk_Alloc: failed", "Couldn't open SDL",
          "ERROR:", "Couldn't load map")

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("label")
ap.add_argument("app")
args = ap.parse_args()

app = Path(args.app).resolve()
binary = app / "Contents/MacOS/QSS-M"
base = Path(tempfile.gettempdir()) / "qssm-engine-workloads" / f"base-{args.label}"
id1 = base / "id1"
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print(f"  {name:<34} {'OK' if ok else 'FAIL'} {detail}", flush=True)


def rcon(cmd):
    body = b"\x05" + PASSWORD.encode() + b"\x00" + cmd.encode() + b"\x00"
    pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(pkt, ("127.0.0.1", PORT))


def batch(*cmds):
    """rcon executes one command per packet; an alias runs several in one command buffer pass."""
    rcon('alias qssm_work_batch "' + "; ".join(cmds) + '"')
    time.sleep(0.3)
    rcon("qssm_work_batch")


def pids():
    r = subprocess.run(["pgrep", "-f", str(base)], capture_output=True, text=True)
    return [int(p) for p in r.stdout.split()]


def wait_for(path, predicate, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists() and predicate(path.read_text(errors="replace")):
            return True
        time.sleep(0.25)
    return False


def wait_exit(timeout):
    deadline = time.time() + timeout
    while pids() and time.time() < deadline:
        time.sleep(0.25)
    return not pids()


def server_map(port, timeout=2.0):
    """Ask a server for its info the way the server browser does; returns its map name or None."""
    body = b"\x02QUAKE\x00\x03"   # CCREQ_SERVER_INFO, game name, NET_PROTOCOL_VERSION
    pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(timeout)
        try:
            s.sendto(pkt, ("127.0.0.1", port))
            data, _ = s.recvfrom(1400)
        except OSError:
            return None
    if len(data) < 6 or data[4] != 0x83:   # CCREP_SERVER_INFO
        return None
    fields = data[5:].split(b"\x00")      # address, hostname, map name, then counts
    return fields[2].decode(errors="replace") if len(fields) > 2 else None


def errors_in(text):
    return [line.strip() for line in text.splitlines() if any(e in line for e in ERRORS)]


def wait_map(expected, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if server_map(PORT) == expected:
            return True
        time.sleep(0.25)
    return False


shutil.rmtree(base, ignore_errors=True)
id1.mkdir(parents=True)
for pak in ("pak0.pak", "pak1.pak"):
    (id1 / pak).symlink_to(PAKS / pak)

try:
    # 1. Listen server: demo record, save/load, screenshot, map change, quit.
    listen_log = base / "listen.txt"
    subprocess.run(["open", "-n", "-a", str(app), "--stdout", str(listen_log), "--stderr", str(listen_log), "--args",
                    "-basedir", str(base), "-noquakeimport", "-nosound", "-port", str(PORT),
                    "-window", "-width", "640", "-height", "480",
                    "+rcon_password", PASSWORD, "+listen", "1", "+map", "start"], check=True)
    check("listen server reached the map", wait_for(listen_log, lambda t: "Quake Initialized" in t, 30))
    time.sleep(6)

    batch("record qssmwork", "echo qssm-work-recording")
    time.sleep(4)
    batch("stop", "echo qssm-work-recorded")
    recorded = wait_for(listen_log, lambda t: "qssm-work-recorded" in t, 10)
    demo = next(iter(id1.glob("**/qssmwork.dem")), None)   # QSS-M records into <gamedir>/demos
    check("demo recorded", recorded and demo is not None and demo.stat().st_size > 1024,
          f"{demo.relative_to(base)}, {demo.stat().st_size} bytes" if demo else "no qssmwork.dem")

    batch("save qssmwork", "echo qssm-work-saved")
    wait_for(listen_log, lambda t: "qssm-work-saved" in t, 10)
    time.sleep(1.5)   # saves are written on a background thread
    save = next(iter(id1.glob("**/qssmwork.sav")), None)
    check("game saved", save is not None and save.stat().st_size > 0, str(save.relative_to(base)) if save else "")

    # Leave the saved map so a failed load cannot pass just by echoing the marker.
    batch("map e1m1", "echo qssm-work-preload")
    check("left saved map before loading", wait_map("e1m1"))
    batch("load qssmwork", "echo qssm-work-loaded")
    check("game loaded", wait_for(listen_log, lambda t: "qssm-work-loaded" in t and "loading game" in t.lower(), 20)
          and wait_map("start"))
    time.sleep(3)

    before = set(id1.glob("**/*.tga")) | set(id1.glob("**/*.png")) | set(id1.glob("**/*.jpg"))
    batch("screenshot", "echo qssm-work-shot")
    deadline = time.time() + 10
    shots = set()
    while time.time() < deadline and not shots:
        shots = (set(id1.glob("**/*.tga")) | set(id1.glob("**/*.png")) | set(id1.glob("**/*.jpg"))) - before
        time.sleep(0.25)
    check("screenshot written", bool(shots), ", ".join(str(p.relative_to(base)) for p in shots))

    batch("map e1m1", "echo qssm-work-mapped")
    check("map change to e1m1", wait_for(listen_log, lambda t: "qssm-work-mapped" in t, 30)
          and wait_map("e1m1"))
    time.sleep(5)
    batch("echo qssm-work-alive")
    check("responsive after map change", wait_for(listen_log, lambda t: "qssm-work-alive" in t, 10))

    rcon("quit")
    check("listen server quit cleanly", wait_exit(20))
    found = errors_in(listen_log.read_text(errors="replace"))
    check("no engine errors (listen)", not found, "; ".join(found[:3]))

    # 2. Demo playback from the command line (rcon timedemo would end the listen server).
    play_log = base / "playdemo.txt"
    subprocess.run(["open", "-n", "-a", str(app), "--stdout", str(play_log), "--stderr", str(play_log), "--args",
                    "-basedir", str(base), "-noquakeimport", "-nosound", "-window", "-width", "640", "-height", "480",
                    "+timedemo", "qssmwork"], check=True)
    played = wait_for(play_log, lambda t: " frames " in t and " fps" in t, 60)
    fps_line = next((l.strip() for l in play_log.read_text(errors="replace").splitlines() if " fps" in l), "")
    check("recorded demo plays back", played, fps_line)
    for p in pids():
        os.kill(p, signal.SIGTERM)
    check("playback client exited on SIGTERM", wait_exit(15))
    found = errors_in(play_log.read_text(errors="replace"))
    check("no engine errors (playback)", not found, "; ".join(found[:3]))

    # 3. Dedicated server with timers.
    ded_log = base / "dedicated.txt"
    with open(ded_log, "w") as log:
        ded = subprocess.Popen([str(binary), "-basedir", str(base), "-noquakeimport", "-dedicated", "2",
                                "-port", str(PORT + 1), "+map", "start"],
                               stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        up = wait_for(ded_log, lambda t: "Quake Initialized" in t, 30)
        mapname = None
        deadline = time.time() + 20
        while up and time.time() < deadline and mapname is None and ded.poll() is None:
            mapname = server_map(PORT + 1)
            if mapname is None:
                time.sleep(0.5)
        time.sleep(3)   # let server frames and timers run
        alive = ded.poll() is None
        ded.send_signal(signal.SIGTERM)
        try:
            code = ded.wait(20)
        except subprocess.TimeoutExpired:
            ded.kill()
            code = "killed"
    text = ded_log.read_text(errors="replace")
    check("dedicated server served the map", up and alive and mapname == "start", f"server info map={mapname}")
    check("dedicated server exited on SIGTERM", code == 128 + signal.SIGTERM, f"exit {code}")
    found = errors_in(text)
    check("no engine errors (dedicated)", not found, "; ".join(found[:3]))
finally:
    for p in pids():
        try:
            os.kill(p, signal.SIGKILL)
        except ProcessLookupError:
            pass

print(f"{args.label}: {'PASS' if results and all(results) else 'FAIL'} ({sum(results)}/{len(results)})")
raise SystemExit(0 if results and all(results) else 1)
