#!/usr/bin/env python3
"""Drive the real QSS-M client through the stress channel and profile frames.

This is intentionally smaller than qssm_stress.py: it does not impose POSIX
resource limits or sandboxing, so it can drive the Windows client too.
Commands still go through the same append-only ``-stress`` file and are
executed by the engine's main thread.

Timing comes from the engine's own ``_stress_frameprof`` command rather than
``r_speeds``.  ``r_speeds`` only times ``R_RenderView`` -- it misses present
and the scene-cache upload that lands on the main thread -- it reports whole
milliseconds, and it prints a line every frame, so under ``-condebug`` the
measurement writes a file per frame and perturbs what it is measuring.
``_stress_frameprof`` samples the whole frame period, buffers it, and reports
percentiles plus scene-cache counters once at the end.

The client is left uncapped (``host_maxfps 0``, ``vid_vsync 0``) so frame time
reflects work rather than the frame limiter.
"""

import argparse
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path


# Reported by the user as spots where ad_tears drops frames.
POSITIONS = {
    "tears_high": (1523, 315, -608, -16, 250, 0),
    "tears_low": (997, -710, -291, -11, 175, 0),
}

# Reference spots to compare the reported ones against. The question is not
# "how fast is this spot" but "what scales between a fast spot and a slow one".
REFERENCE = {
    "tears_start": (-1387, -1300, -220, 0, 90, 0),
}

# Each variant is a list of console commands applied before measuring.
VARIANTS = {
    # The fix: a lightstyle tick refreshes lightmaps only.
    "cache_on": ["r_scenecache 1"],
    "cache_off": ["r_scenecache 0"],
}

# Subsystem bisect: which part of the frame actually scales between a fast spot
# and a slow one. Each disables one thing against an otherwise stock frame.
BISECT = {
    "base":       ["r_drawworld 1", "r_drawentities 1", "r_particles 1"],
    "no_ents":    ["r_drawworld 1", "r_drawentities 0", "r_particles 1"],
    "no_world":   ["r_drawworld 0", "r_drawentities 1", "r_particles 1"],
    "no_parts":   ["r_drawworld 1", "r_drawentities 1", "r_particles 0"],
    "world_only": ["r_drawworld 1", "r_drawentities 0", "r_particles 0"],
}
VARIANTS.update(BISECT)

# Entity rendering dominates the frame in ad_tears; this narrows down which
# part of it.
#
# Every variant must restate EVERY cvar the group touches. Variants run against
# one long-lived client, so a variant that only sets what it disables leaves
# that setting applied for everything after it -- which silently turned shadows
# off for most of an earlier bisect and made it look like no single feature
# mattered.
_ENT_DEFAULTS = ["r_drawentities 1", "r_shadows 0.1", "gl_powerupshells 2",
                 "r_lerpmodels 3", "r_lerpmove 1", "r_drawviewmodel 1",
                 "r_grass 1", "r_particles 1", "r_alias_skip 0",
                 "r_shadows_maxdist 0", "r_shadows_bmodels 1",
                 "r_shadows_buffered 1"]

def _entvariant(**overrides):
    cmds = []
    for c in _ENT_DEFAULTS:
        name = c.split()[0]
        cmds.append("{} {}".format(name, overrides[name])
                    if name in overrides else c)
    return cmds

ENTBISECT = {
    "e_base":     _entvariant(),
    "e_noshadow": _entvariant(r_shadows="0"),
    "e_noshell":  _entvariant(gl_powerupshells="0"),
    "e_nolerp":   _entvariant(r_lerpmodels="0", r_lerpmove="0"),
    "e_noview":   _entvariant(r_drawviewmodel="0"),
    "e_nograss":  _entvariant(r_grass="0"),
    "e_noparts":  _entvariant(r_particles="0"),
    "e_nolight":  _entvariant(r_alias_skip="1"),
    "e_noents":   _entvariant(r_drawentities="0"),
    # shadow distance cull, off by default
    "e_sdist512":  _entvariant(r_shadows_maxdist="512"),
    "e_sdist1024": _entvariant(r_shadows_maxdist="1024"),
    # isolate what remains once alias shadows are culled: brush shadows and the
    # full-screen stencil clear the shadow pass does before drawing anything.
    "e_nobmodel":  _entvariant(r_shadows_bmodels="0"),
    # identical shadows, only the submission path differs
    "e_immediate": _entvariant(r_shadows_buffered="0"),
    "e_buffered":  _entvariant(r_shadows_buffered="1"),
    "e_shadowoff": _entvariant(r_shadows_bmodels="0"),
    "e_sdist_nobm": _entvariant(r_shadows_maxdist="512", r_shadows_bmodels="0"),
}
VARIANTS.update(ENTBISECT)

ALIASBISECT = {
    "a_base":    ["r_drawentities 1", "r_alias_skip 0"],
    "a_nolight": ["r_drawentities 1", "r_alias_skip 1"],
}
VARIANTS.update(ALIASBISECT)

# The bottom line: everything as it shipped, vs everything default-on, vs
# default-on plus the opt-in distance cull. Same client, interleaved.
TOTALS = {
    "stock":      ["r_shadows_buffered 0", "r_shadows_maxdist 0"],
    "fixed":      ["r_shadows_buffered 1", "r_shadows_maxdist 0"],
    "fixed_cull": ["r_shadows_buffered 1", "r_shadows_maxdist 1024"],
}
VARIANTS.update(TOTALS)

PROF_RE = re.compile(r"^STRESS_PROF label=(\S+) (.*)$")
CACHE_RE = re.compile(r"^STRESS_PROF_CACHE label=(\S+) (.*)$")


def parse_kv(text):
    out = {}
    for token in text.split():
        if "=" not in token:
            continue
        key, _, value = token.partition("=")
        try:
            out[key] = float(value) if "." in value else int(value)
        except ValueError:
            out[key] = value
    return out


def set_affinity(pid, cores):
    """Restrict the client to `cores` CPUs, to emulate a weaker machine.

    The scene cache offloads rebuilds to a worker thread, so its cost is
    invisible on a box with spare cores.  Squeezing the process is how you find
    out what happens when the worker cannot keep up.
    """
    import ctypes

    handle = ctypes.windll.kernel32.OpenProcess(0x0200 | 0x0400, False, pid)
    if not handle:
        raise OSError("OpenProcess failed for pid {}".format(pid))
    try:
        mask = (1 << cores) - 1
        if not ctypes.windll.kernel32.SetProcessAffinityMask(
                handle, ctypes.c_size_t(mask)):
            raise OSError("SetProcessAffinityMask failed")
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


class LiveClient:
    def __init__(self, binary, basedir, work, width, height, cores=0):
        self.binary = Path(binary).resolve()
        self.basedir = Path(basedir).resolve()
        self.work = Path(work).resolve()
        self.work.mkdir(parents=True, exist_ok=True)
        self.script = self.work / "stress.cmds"
        # Windows GUI builds do not have a dependable redirected stdout.  The
        # engine's native -condebug log is flushed through the same console
        # path used by the client and lives beside the executable.
        self.logpath = self.binary.parent / "qconsole.log"
        self.script.write_text("", encoding="utf-8")
        try:
            self.logpath.unlink()
        except FileNotFoundError:
            pass
        self.seq = 0
        self.proc = None
        self.log_offset = 0
        self.width = width
        self.height = height
        self.cores = cores

    def start(self):
        args = [
            str(self.binary),
            "-basedir", str(self.basedir),
            "-stress", str(self.script),
            "-condebug", "20",
            "-noudp", "-noice",
            "-nosound", "-window",
            "-width", str(self.width), "-height", str(self.height),
        ]
        self.proc = subprocess.Popen(
            args, cwd=str(self.binary.parent),
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
        )
        if self.cores:
            set_affinity(self.proc.pid, self.cores)
        self.wait_for("STRESS_READY", 30)

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def read_new(self):
        try:
            with self.logpath.open("r", encoding="utf-8", errors="replace") as fh:
                fh.seek(self.log_offset)
                text = fh.read()
                self.log_offset = fh.tell()
                return text
        except OSError:
            return ""

    def wait_for(self, needle, timeout):
        deadline = time.monotonic() + timeout
        seen = []
        while time.monotonic() < deadline:
            chunk = self.read_new()
            if chunk:
                seen.append(chunk)
                if needle in "".join(seen):
                    return "".join(seen)
            if self.proc is not None and self.proc.poll() is not None:
                raise RuntimeError(f"client exited with code {self.proc.returncode}")
            time.sleep(0.02)
        raise TimeoutError(f"timed out waiting for {needle!r}")

    def send(self, *commands):
        self.seq += 1
        with self.script.open("a", encoding="utf-8") as fh:
            fh.write(f"@seq {self.seq}\n")
            for command in commands:
                fh.write(command.rstrip("\n") + "\n")
            fh.flush()
        return self.seq

    def barrier(self, timeout=60):
        """Block until every command sent so far has been executed."""
        seq = self.send("_stress_status {}".format(self.seq + 1))
        return self.wait_for("STRESS_STATUS seq={}".format(seq), timeout)

    def settle(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.read_new()
            if not self.alive():
                break
            time.sleep(0.05)
        self.read_new()

    def wait_spawned(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.barrier(min(60, max(1, deadline - time.monotonic())))
            if "signon=4" in text:
                return
            time.sleep(0.25)
        raise TimeoutError("timed out waiting for a fully spawned client")

    def profile(self, label, seconds, timeout=None, motion=None, origin=None,
                span=256.0, step=24.0):
        """Sample `seconds` of frames.

        motion:
          None/"still" -- stand still.  Only exercises the lightstyle path.
          "sweep"      -- teleport along X in `step` increments.  Changes the
                          PVS, but each setpos is a console command with its own
                          cost, so "storm" is the control for that.
          "storm"      -- re-setpos to the *same* spot at the same rate.  Isolates
                          what the sweep's command traffic costs on its own.
          "walk"       -- hold +forward.  Real player movement and the only mode
                          whose PVS churn matches what a player actually sees.
        """
        if motion == "walk":
            self.send("+forward")
        self.send("_stress_frameprof {} {}".format(seconds, label))
        if motion in ("sweep", "storm"):
            end = time.monotonic() + seconds
            offset, direction = 0.0, 1.0
            while time.monotonic() < end and self.alive():
                if motion == "sweep":
                    offset += direction * step
                    if abs(offset) >= span:
                        direction = -direction
                moved = list(origin)
                moved[0] += offset
                self.send("setpos " + " ".join(str(v) for v in moved))
                time.sleep(0.03)
        # Wait on STRESS_PROF: it is emitted whenever QSSM_STRESS is defined.
        # STRESS_PROF_CACHE only appears when the renderer counter patch is also
        # compiled in (QSSM_RENDERSTATS), so it is parsed opportunistically.
        text = self.wait_for(
            "STRESS_PROF label={}".format(label),
            timeout if timeout is not None else seconds + 60,
        )
        self.settle(0.2)
        text += self.read_new()
        if motion == "walk":
            self.send("-forward")
            self.barrier()
        sample = {}
        for line in text.splitlines():
            match = PROF_RE.match(line.strip())
            if match and match.group(1) == label:
                sample.update(parse_kv(match.group(2)))
            match = CACHE_RE.match(line.strip())
            if match and match.group(1) == label:
                sample.update(parse_kv(match.group(2)))
        return sample

    def stop(self):
        if self.proc is None or self.proc.poll() is not None:
            return
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)


def summarise(samples):
    """Collapse repeats of one (variant, position) cell into medians."""
    out = {}
    for key in ("fps", "p50", "p90", "p99", "max", "builds", "stylebuilds",
                "uploads", "uploadkb", "uploadms", "blocks", "blockms",
                "skyscanms", "workerwallms", "lightmapwallms", "lmscanned", "lmbuilt", "stylejobs", "stylebusy", "visms", "queuems", "drawms", "visfat", "edicts", "aents", "akeysw", "adkey", "adModel",
                "dcMain", "dcShadow", "dcOutline", "dcShell",
                "trMain", "trShadow", "trOutline", "trShell",
                "partms", "partcount", "scullA", "scullD", "bsEnts", "bsSurfs", "bsVerts", "bsBuf", "bsIdx",
                "frames", "over16", "over33"):
        values = [s[key] for s in samples if key in s]
        if values:
            out[key] = round(statistics.median(values), 2)
    out["repeats"] = len(samples)
    return out


def run(args):
    work = Path(args.output).resolve()
    client = LiveClient(args.bin, args.basedir, work, args.width,
                        args.height, args.cores)
    positions = dict(POSITIONS)
    positions.update(REFERENCE)
    for spec in args.pos or []:
        name, _, values = spec.partition("=")
        positions[name] = tuple(float(v) for v in values.replace(",", " ").split())
    variants = {k: v for k, v in VARIANTS.items()
                if not args.variant or k in args.variant}
    if args.position:
        # The cache pool accumulates, so the lightstyle sweep's union grows with
        # every distinct spot visited. Restricting to one position measures what
        # standing in one place actually costs.
        positions = {k: v for k, v in positions.items() if k in args.position}

    report = {
        "binary": str(client.binary),
        "basedir": str(client.basedir),
        "map": args.map,
        "resolution": [args.width, args.height],
        "cores": args.cores,
        "sample_seconds": args.sample_seconds,
        "repeats": args.repeats,
        "raw": [],
        "summary": {},
    }
    cells = {}
    try:
        client.start()
        # No r_speeds: its per-frame Con_Printf is itself a confounder here.
        client.send("host_maxfps 0", "vid_vsync 0", "r_speeds 0",
                    "map {}".format(args.map))
        client.wait_spawned(args.map_timeout)
        client.settle(args.settle)

        # Interleave repeats so warm-up and thermal drift hit every cell
        # equally instead of loading onto whichever variant ran first.
        for rep in range(args.repeats):
            for vname, commands in variants.items():
                client.send(*commands)
                client.barrier()
                for pname, values in positions.items():
                    label = "{}_{}_{}".format(vname, pname, rep)
                    client.send("setpos " + " ".join(str(v) for v in values))
                    client.barrier()
                    client.settle(args.warmup)
                    sample = client.profile(
                        label, args.sample_seconds, motion=args.motion,
                        origin=values, span=args.motion_span,
                        step=args.motion_step)
                    sample.update({"variant": vname, "position": pname,
                                   "repeat": rep, "setpos": list(values)})
                    report["raw"].append(sample)
                    cells.setdefault((vname, pname), []).append(sample)
                    show = lambda k: str(sample.get(k, "-"))
                    print("  {:<28} fps={:<7} p50={:<6} p99={:<7} max={:<7} "
                          "ents={:<5} keysw={:<5} dkey={:<5} dmdl={:<4} dc={}/{}/{}/{} tr={}/{}/{}/{} part={}ms/{} scull={}/{} bshadow={}ent/{}buf/{}idx".format(
                              label, show("fps"), show("p50"), show("p99"),
                              show("max"),
                              show("aents"), show("akeysw"), show("adkey"),
                              show("adModel"),
                              show("dcMain"), show("dcShadow"), show("dcOutline"),
                              show("dcShell"),
                              show("trMain"), show("trShadow"), show("trOutline"),
                              show("trShell"),
                              show("partms"), show("partcount"),
                              show("scullA"), show("scullD"),
                              show("bsEnts"), show("bsBuf"), show("bsIdx")),
                          flush=True)
    finally:
        client.stop()
        for (vname, pname), samples in cells.items():
            report["summary"].setdefault(vname, {})[pname] = summarise(samples)
        (work / "report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True, help="path to the client executable")
    parser.add_argument("--basedir", required=True, help="QSS-M base directory")
    parser.add_argument("--output", required=True, help="probe artifact directory")
    parser.add_argument("--map", default="ad_tears")
    parser.add_argument("--pos", action="append",
                        help="extra position as NAME=x,y,z,pitch,yaw,roll")
    parser.add_argument("--variant", action="append",
                        help="restrict to these variants (default: all)")
    parser.add_argument("--position", action="append",
                        help="restrict to these positions (default: all)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--map-timeout", type=float, default=180)
    parser.add_argument("--settle", type=float, default=3)
    parser.add_argument("--warmup", type=float, default=1.5,
                        help="seconds to idle after setpos before sampling")
    parser.add_argument("--sample-seconds", type=float, default=5)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cores", type=int, default=0,
                        help="restrict the client to N CPUs (0 = no limit)")
    parser.add_argument("--motion", default="still",
                        choices=("still", "sweep", "storm", "walk"),
                        help="how the viewpoint moves while sampling")
    parser.add_argument("--motion-span", type=float, default=256,
                        help="sweep amplitude in units")
    parser.add_argument("--motion-step", type=float, default=16,
                        help="units moved per step during a motion sweep")
    args = parser.parse_args(argv)
    try:
        report = run(args)
    except (OSError, RuntimeError, TimeoutError) as exc:
        print(f"live probe failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
