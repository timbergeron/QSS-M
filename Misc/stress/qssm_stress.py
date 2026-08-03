#!/usr/bin/env python3
"""
QSS-M stress tester / crash finder.

Drives a real QSS-M build through randomized sessions -- connecting to a listen
server, changing maps, playing, poking every menu, running demos, and firing
fuzzed console commands -- and reports anything that crashes, wedges or dies
with a fatal engine error.

Requires an engine built with the "-stress" hooks (see Host_StressInit in
host.c).  The harness talks to the engine over two channels:

  * the stress script    -- an append-only command file the engine polls every
                            frame; works in *every* client state, including
                            disconnected, in-menu, and demo playback
  * rcon over UDP        -- only usable while a listen server is up, kept
                            because it exercises the real network command path

Crash oracles:
  * process death by signal
  * a fresh ~/Library/Logs/DiagnosticReports/QSS-M-*.ips report
  * Sys_Error / Host_Error / overflow text in the console log
  * loss of liveness (no _stress_status reply within the hang timeout)

Usage:
    ./qssm_stress.py --list
    ./qssm_stress.py --minutes 20
    ./qssm_stress.py --scenario menu --runs 5 --seed 1234
    ./qssm_stress.py --jobs 5 --stress-level deep --minutes 20
    ./qssm_stress.py --replay tmp/results/0003/repro.txt
"""

import argparse
import atexit
import glob
import hashlib
import json
import os
import random
import re
import resource
import secrets
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

try:
    import fcntl
except ImportError:  # pragma: no cover - QSS-M stress runs on POSIX hosts
    fcntl = None

# The harness is disposable tooling; keep interpreter caches out of the
# repository tree alongside the other generated artifacts in tmp/.
sys.dont_write_bytecode = True

HOME = Path.home()
REPO = Path(__file__).resolve().parents[2]

DEFAULT_BIN_GLOB = str(
    HOME / "Library/Developer/Xcode/DerivedData/QuakeSpasm-*/Build/Products/*/QSS-M.app/Contents/MacOS/QSS-M"
)
DEFAULT_PAKDIRS = [
    HOME / "Desktop/qssm/id1/paks",
    HOME / "Desktop/qssm/id1",
    HOME / "Games/Quake/id1",
]
CRASH_DIR = HOME / "Library/Logs/DiagnosticReports"

# ---------------------------------------------------------------------------
# key codes (keys.h)
# ---------------------------------------------------------------------------
K = {
    "TAB": 9, "ENTER": 13, "ESCAPE": 27, "SPACE": 32, "BACKSPACE": 127,
    "UP": 128, "DOWN": 129, "LEFT": 130, "RIGHT": 131,
    "ALT": 132, "CTRL": 133, "SHIFT": 134,
    "F1": 135, "F2": 136, "F3": 137, "F4": 138, "F5": 139, "F6": 140,
    "F10": 144, "F12": 146,
    "INS": 147, "DEL": 148, "PGDN": 149, "PGUP": 150, "HOME": 151, "END": 152,
    "MOUSE1": 200, "MOUSE2": 201, "MOUSE3": 202,
    "MWHEELUP": 239, "MWHEELDOWN": 240,
}
NAV_KEYS = [K["UP"], K["DOWN"], K["LEFT"], K["RIGHT"], K["ENTER"], K["ESCAPE"],
            K["BACKSPACE"], K["DEL"], K["TAB"], K["HOME"], K["END"],
            K["PGUP"], K["PGDN"], K["INS"], K["SPACE"],
            K["MWHEELUP"], K["MWHEELDOWN"]]

# menucommands[] from menu.c.  The download/serverlist ones reach out to the
# internet, so they are opt-in.
MENUS_LOCAL = [
    "menu_main", "menu_modmenu", "menu_singleplayer", "menu_load", "menu_save",
    "menu_skill", "menu_multiplayer", "menu_setup", "menu_options", "menu_keys",
    "menu_mouse", "menu_controller", "menu_controller_test", "menu_weaponwheel",
    "menu_sound", "menu_voip", "menu_game", "menu_hud", "menu_crosshair",
    "menu_console", "menu_colorpicker", "menu_startup", "menu_demooptions",
    "menu_pakloading", "menu_modelviewer", "menu_audiobrowser", "menu_saving",
    "menu_misc", "menu_shortcuts", "menu_version", "menu_config", "menu_video",
    "menu_graphics", "help", "menu_credits", "menu_namemaker", "namemaker",
    "menu_mods", "menu_demos", "menu_maps", "menu_bookmarks", "bookmark",
    "menu_history",
]
MENUS_NET = ["menu_slist", "menu_downloadmods", "menu_downloadmaps"]

SHAREWARE_MAPS = ["start", "e1m1", "e1m2", "e1m3", "e1m4", "e1m5", "e1m6", "e1m7", "e1m8",
                  "dm1", "dm2", "dm3", "dm4", "dm5", "dm6", "end"]
REGISTERED_MAPS = ["e2m1", "e2m2", "e2m3", "e2m4", "e2m5", "e2m6", "e2m7",
                   "e3m1", "e3m2", "e3m3", "e3m4", "e3m5", "e3m6", "e3m7",
                   "e4m1", "e4m2", "e4m3", "e4m4", "e4m5", "e4m6", "e4m7", "e4m8"]
BUILTIN_DEMOS = ["demo1", "demo2", "demo3"]

# Fatal / suspicious strings to watch for in the console log.
FATAL_PATTERNS = [
    (re.compile(r"^QUAKE ERROR: (.*)$", re.M), "sys_error"),   # Sys_Error banner
    (re.compile(r"Fatal error", re.I), "sys_error"),
    (re.compile(r"^Host_Error: (.*)$", re.M), "host_error"),
    # the genuinely-bad SZ_GetSpace variants: these raise Host_Error/Sys_Error.
    # A bare "SZ_GetSpace: overflow" is the allowoverflow==true branch -- the
    # engine clears the buffer and drops the client, which is handled behaviour.
    (re.compile(r"SZ_GetSpace: overflow without allowoverflow set"), "sz_overflow_unhandled"),
    (re.compile(r"SZ_GetSpace: \d+ is > full buffer size"), "sz_oversize"),
    (re.compile(r"Hunk_(Alloc|HighAlloc|TempAlloc): failed"), "hunk_exhausted"),
    (re.compile(r"Cache_Alloc: .*failed"), "cache_fail"),
    (re.compile(r"Z_Malloc: failed"), "zmalloc_fail"),
    (re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:"), "sanitizer"),
    (re.compile(r"stack smashing|malloc: \*\*\*|double free|pointer being freed"), "libc_abort"),
    # parser invariant, reported by Host_StressNoteParse: the client either
    # consumed exactly cursize or it walked off the end of the message
    (re.compile(r"^STRESS_PARSE (overread|short) readcount=\d+ cursize=\d+$", re.M), "parse_invariant"),
]
# Host_Error strings that are just the engine correctly rejecting our garbage.
BENIGN_HOST_ERROR = re.compile(
    r"no such (map|demo|file)|couldn't|could not|not found|unknown|"
    r"^Host_Error: Server disconnected|CL_ParseServerMessage|"
    r"^Host_Error: no such command", re.I)

# Commands we must never hand to the fuzzer: they end the run, hammer the
# network, or rewrite files outside the sandbox.
COMMAND_BLOCKLIST = {
    "quit", "exit", "crash", "error", "reconnect_slow",
    "connect", "join", "observe", "spectate", "qwurl", "qeurl",
    "webcheck", "update", "download", "downloadmap", "downloadmods",
    "slist", "search", "serverlist", "ping", "pings", "master",
    "sendrcon", "rcon", "iplog_export", "screenshot", "envmap",
    "vid_restart", "vid_test", "fullscreen", "windowed",
    "toggleconsole", "condump", "logfile", "cfgbackup", "cfgrestore",
    "unbindall", "menu_restart", "sys_open", "openurl", "opendir",
    "showfile", "explore", "record", "timedemo", "startdemos",
}
CVAR_BLOCKLIST_RE = re.compile(
    r"^(vid_|_vid|snd_device|host_maxfps$|developer$|sv_public|rcon_|"
    r"cl_web|net_|sys_|com_|registered$|cmdline$)")


def now():
    return time.monotonic()


# ---------------------------------------------------------------------------
# environment discovery
# ---------------------------------------------------------------------------
def find_binary(explicit=None):
    if explicit:
        p = Path(explicit).expanduser()
        if not p.exists():
            sys.exit(f"binary not found: {p}")
        return p.resolve()
    env = os.environ.get("QSSM_BIN")
    if env:
        return find_binary(env)
    cands = [Path(p) for p in glob.glob(DEFAULT_BIN_GLOB)]
    if not cands:
        sys.exit("no QSS-M build found; pass --bin or set QSSM_BIN")
    cands.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return cands[0]


def find_paks(explicit=None):
    dirs = [Path(explicit).expanduser().resolve()] if explicit else DEFAULT_PAKDIRS
    for d in dirs:
        if (d / "pak0.pak").exists():
            paks = [d / "pak0.pak"]
            if (d / "pak1.pak").exists():
                paks.append(d / "pak1.pak")
            return paks
    sys.exit("no pak0.pak found; pass --paks <dir>")


def find_loose_root(explicit=None, paks=None):
    """Find optional loose game assets needed alongside the PAKs.

    Some QSS-M installs keep gfx.wad and replacement assets outside the PAK
    directory.  Link those read-only assets into each throwaway sandbox rather
    than silently launching an incomplete game root.
    """
    if explicit:
        root = Path(explicit).expanduser()
        if not (root / "gfx.wad").exists():
            sys.exit(f"loose root has no gfx.wad: {root}")
        return root

    candidates = []
    if paks:
        paks_dir = Path(paks[0]).parent
        candidates += [paks_dir / "paktest", paks_dir.parent / "paktest"]
    candidates += [HOME / "Desktop/qssm/id1/paktest"]
    for root in candidates:
        if (root / "gfx.wad").exists():
            return root
    return None


# ---------------------------------------------------------------------------
# crash report watching
# ---------------------------------------------------------------------------
class CrashReports:
    """Watches the macOS DiagnosticReports folder for new QSS-M crashes."""

    def __init__(self):
        self.seen = set(self._list())

    def _list(self):
        if not CRASH_DIR.is_dir():
            return []
        return [p for p in CRASH_DIR.glob("QSS-M*.ips")]

    @staticmethod
    def report_pid(path):
        try:
            _, _, body = path.read_text(errors="replace").partition("\n")
            return json.loads(body).get("pid")
        except Exception:
            return None

    def poll(self, wait=0.0, pid=None):
        """Return newly written crash reports, optionally waiting for one.

        DiagnosticReports is a machine-global directory, so a report is only
        ours if its recorded pid matches -- otherwise a crash from another
        instance (or another harness process) gets pinned on this run.
        """
        deadline = now() + wait
        while True:
            fresh = [p for p in self._list() if p not in self.seen]
            self.seen.update(fresh)
            if pid is not None:
                fresh = [p for p in fresh if self.report_pid(p) == pid]
            if fresh or now() >= deadline:
                return sorted(fresh, key=lambda p: p.stat().st_mtime)
            time.sleep(0.5)

    @staticmethod
    def summarize(path):
        """Pull the faulting signal + top engine frames out of an .ips report."""
        try:
            raw = path.read_text(errors="replace")
        except OSError as exc:
            return {"signal": "?", "frames": [], "error": str(exc)}
        head, _, body = raw.partition("\n")
        try:
            data = json.loads(body)
        except Exception:
            return {"signal": "?", "frames": [], "raw": raw[:4000]}

        exc = data.get("exception", {})
        sig = data.get("termination", {}).get("indicator") or exc.get("signal") or exc.get("type", "?")
        images = data.get("usedImages", [])
        frames = []
        faulting = None
        for th in data.get("threads", []):
            if th.get("triggered"):
                faulting = th
                break
        if faulting is None and data.get("threads"):
            faulting = data["threads"][0]
        for fr in (faulting or {}).get("frames", [])[:24]:
            idx = fr.get("imageIndex", -1)
            img = images[idx].get("name", "?") if 0 <= idx < len(images) else "?"
            sym = fr.get("symbol")
            if sym:
                frames.append(f"{img}`{sym}+{fr.get('symbolLocation', 0)}")
            else:
                frames.append(f"{img}+0x{fr.get('imageOffset', 0):x}")
        return {"signal": str(sig), "frames": frames}


# ---------------------------------------------------------------------------
# rcon
# ---------------------------------------------------------------------------
CCREQ_RCON = 0x05


class Rcon:
    def __init__(self, port, password="stresspw", host="127.0.0.1"):
        self.addr = (host, port)
        self.password = password
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.6)

    def send(self, cmd, want_reply=False):
        body = b"\x05" + self.password.encode() + b"\x00" + cmd.encode() + b"\x00"
        pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
        try:
            self.sock.sendto(pkt, self.addr)
        except OSError:
            return None
        if not want_reply:
            return None
        try:
            data, _ = self.sock.recvfrom(8192)
            return data[5:].decode(errors="replace")
        except socket.timeout:
            return None

    def close(self):
        self.sock.close()


# ---------------------------------------------------------------------------
# sandbox
# ---------------------------------------------------------------------------
QUAKE_RC = """// stress harness -- no startdemos, so the client stays where we put it
exec default.cfg
exec autoexec.cfg
"""

AUTOEXEC = """// stress harness defaults
host_maxfps 72
scr_conspeed 100000
con_notifytime 1
sv_public 0
"""

# Blank the web-download mirrors unless --allow-net: a corrupted demo's precache
# list will otherwise send real HTTP requests for attacker-named files.
OFFLINE_CFG = [
    'cl_web_download_url ""',
    'cl_web_download_url2 ""',
]


class Sandbox:
    """A throwaway -basedir so runs never touch the user's Quake install."""

    def __init__(self, root, paks, autoexec_extra=(), loose_root=None):
        self.root = Path(root).expanduser().resolve()
        self.paks = paks
        self.autoexec_extra = list(autoexec_extra)
        self.loose_root = Path(loose_root) if loose_root else None

    def build(self):
        if self.root.exists():
            shutil.rmtree(self.root)
        id1 = self.root / "id1"
        id1.mkdir(parents=True)
        for p in self.paks:
            # PAKs are normally read-only, but an attacker-controlled path
            # must not be able to turn `id1/pak0.pak` into a write-through to
            # the user's real install either.
            shutil.copy2(p, id1 / p.name)

        # Copy loose assets into the sandbox.  These directories are not
        # read-only from the engine's point of view: filefuzz deliberately
        # replaces maps/models/sounds in them.  Symlinking them would turn a
        # mutant write or unlink into a write to the user's real install.
        if self.loose_root:
            for name in ("gfx.wad", "gfx", "maps", "progs", "sound",
                         "textures", "particles", "csprogs.dat"):
                source = self.loose_root / name
                target = id1 / name
                if source.exists() and not target.exists():
                    if source.is_dir():
                        shutil.copytree(source, target, symlinks=False)
                    else:
                        shutil.copy2(source, target)

        (id1 / "quake.rc").write_text(QUAKE_RC)
        (id1 / "autoexec.cfg").write_text(AUTOEXEC + "\n".join(self.autoexec_extra) + "\n")
        for sub in ("demos", "screenshots", "backups", "configs", "locs"):
            (id1 / sub).mkdir(exist_ok=True)
        return self.root


class EventLog:
    """Ordered, append-only events shared by all actors in one run."""

    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.sequence = 0

    def record(self, event_type, **fields):
        self.sequence += 1
        event = {"seq": self.sequence, "type": event_type, **fields}
        with self.path.open("a") as fh:
            fh.write(json.dumps(event, sort_keys=True) + "\n")
            fh.flush()
        return event


class CampaignLock:
    """Prevent two campaigns from sharing ports, GL, or result directories."""

    def __init__(self, path):
        self.path = Path(path)
        self.fh = None

    def acquire(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.fh = self.path.open("a+")
        if fcntl is None:
            return
        try:
            fcntl.flock(self.fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            self.fh.close()
            self.fh = None
            raise RuntimeError(f"campaign already running: {self.path}") from exc

    def release(self):
        if self.fh is None:
            return
        if fcntl is not None:
            fcntl.flock(self.fh.fileno(), fcntl.LOCK_UN)
        self.fh.close()
        self.fh = None


def load_repro(path):
    """Load a legacy text journal or the canonical multi-actor JSONL log."""
    path = Path(path)
    lines = path.read_text(errors="replace").splitlines()
    if path.suffix == ".jsonl" or next((line for line in lines if line.strip()), "").startswith("{"):
        try:
            return [json.loads(line) for line in lines if line.strip()]
        except json.JSONDecodeError as exc:
            raise ValueError(f"invalid event log {path}: {exc}") from exc
    return lines


# ---------------------------------------------------------------------------
# engine instance
# ---------------------------------------------------------------------------
class Dead(Exception):
    """The engine went away (crash, fatal error, or unexpected exit)."""


class Hung(Exception):
    """The engine stopped answering liveness probes."""


class PortBusy(Exception):
    """The engine could not bind its configured local UDP port."""


def apply_child_limits(cfg):
    """Apply conservative per-engine limits in the child after fork."""
    def limit(name, soft, hard=None):
        if not hasattr(resource, name):
            return
        try:
            kind = getattr(resource, name)
            old_soft, old_hard = resource.getrlimit(kind)
            requested_hard = hard if hard is not None else soft
            if old_hard != resource.RLIM_INFINITY:
                requested_hard = min(requested_hard, old_hard)
            requested_soft = min(soft, requested_hard)
            resource.setrlimit(kind, (requested_soft, requested_hard))
        except (OSError, ValueError):
            # A platform may expose a limit but refuse to lower it for this
            # process.  The optional sandbox profile remains available.
            pass

    cpu_limit = getattr(cfg, "cpu_limit", 300)
    file_limit_mb = getattr(cfg, "file_limit_mb", 128)
    fd_limit = getattr(cfg, "fd_limit", 256)
    memory_limit_mb = getattr(cfg, "memory_limit_mb", 4096)
    process_limit = getattr(cfg, "process_limit", 0)
    limit("RLIMIT_CPU", cpu_limit, cpu_limit + 5)
    limit("RLIMIT_FSIZE", file_limit_mb * 1024 * 1024)
    limit("RLIMIT_NOFILE", fd_limit)
    if memory_limit_mb:
        limit("RLIMIT_AS", memory_limit_mb * 1024 * 1024)
    if process_limit:
        limit("RLIMIT_NPROC", process_limit)


def sandbox_profile(cfg, work):
    """Write a macOS Seatbelt profile for one loopback-only run."""
    def q(path):
        return str(Path(path).resolve()).replace("\\", "\\\\").replace('"', '\\"')

    binary = Path(cfg.binary).resolve()
    readable = {binary.parent, work, Path(cfg.paks[0]).parent}
    if len(binary.parents) >= 3:
        readable.add(binary.parents[2])  # QSS-M.app bundle
    if getattr(cfg, "loose_root", None):
        readable.add(Path(cfg.loose_root))
    lines = [
        "(version 1)",
        "(deny default)",
        '(import "system.sb")',
        "(allow file-read* (subpath \"/System/Library\"))",
        "(allow file-read* (subpath \"/usr/lib\"))",
        "(allow file-read* (subpath \"/Library/Frameworks\"))",
        "(allow file-read* (subpath \"/private/etc\"))",
        "(deny file-write* (subpath \"/private/var\"))",
        "(deny file-write* (subpath \"/private/tmp\"))",
        "(deny process-fork)",
        f'(allow process-exec (literal "{q(binary)}"))',
        "(allow mach-lookup)",
        "(allow mach-register)",
        "(allow sysctl-read)",
        "(allow ipc-posix-shm-read* ipc-posix-shm-write*)",
        f'(allow file-read-metadata (path-ancestors "{q(binary)}"))',
        f'(allow file-read-metadata (path-ancestors "{q(work)}"))',
    ]
    for path in sorted(readable, key=str):
        lines.append(f'(allow file-read* (subpath "{q(path)}"))')
    lines += [
        f'(allow file-write* (subpath "{q(work)}"))',
        '(allow network-inbound (local ip "localhost:*"))',
        '(allow network-outbound (remote ip "localhost:*"))',
    ]
    profile = work / "stress.sb"
    profile.write_text("\n".join(lines) + "\n")
    return profile


class Engine:
    PORT_RETRY_LIMIT = 4

    def __init__(self, cfg, workdir, port, extra_args=(), actor="client", event_log=None):
        self.cfg = cfg
        self.work = Path(workdir)
        self.port = port
        self.extra_args = list(extra_args)
        self.actor = actor
        self.scenario = actor
        self.event_log = event_log
        self.script = self.work / "stress.cmds"
        self.logpath = self.work / "console.log"
        self.journal = []
        self.proc = None
        self._logfh = None
        self._logpos = 0
        self._logbuf = ""
        self._coverage_buf = ""
        self.coverage_input = None
        self.coverage_sink = getattr(cfg, "coverage_sink", None)
        self._seq = 0
        self.throttle_events = 0
        self.dialogs_answered = 0
        self._cmdseq = 0
        self.last_acked = 0
        self.last_exec = 0
        self.fed_corrupt_input = False   # set by scenarios that hand the engine garbage
        self.expected_errors = set()
        self.assertion_failure = None     # scenario-level positive oracle failure
        self.helpers = []          # extra engines this run owns (dedicated servers)
        self.rcon = Rcon(port, password=getattr(cfg, "rcon_password", "stresspw"))

    def set_coverage_input(self, data, adapter="raw", input_sha=None):
        """Associate the next parser telemetry with the bytes just sent."""
        if isinstance(data, str):
            data = data.encode("utf-8", "surrogateescape")
        data = bytes(data)
        digest = input_sha or hashlib.sha256(data).hexdigest()
        self.coverage_input = {
            "sha256": digest, "data": data, "adapter": adapter,
        }
        return digest

    def clear_coverage_input(self):
        self.coverage_input = None

    def corpus_seeds(self, adapter=None, limit=32, rng=None):
        """Load a bounded set of prior novelty inputs for mutation."""
        root = Path(getattr(self.cfg, "corpus", ""))
        if not root.exists():
            return []
        paths = list((root / adapter).glob("*.bin")) if adapter else list(root.glob("*/*.bin"))
        (rng or random).shuffle(paths)
        seeds = []
        for path in paths[:limit]:
            try:
                data = path.read_bytes()
            except OSError:
                continue
            if data:
                seeds.append((path.stem, data))
        return seeds

    # -- lifecycle ---------------------------------------------------------
    def start(self, boot_cmds=()):
        if self._logfh is not None:
            self._logfh.close()
            self._logfh = None
        self._logbuf = ""
        self._coverage_buf = ""
        self.script.write_text("")
        # Keep relative paths, HOME-based config, and temporary files inside
        # this run.  Besides making cleanup predictable, this gives the
        # filesystem oracle a real boundary to inspect.
        run_home = self.work / "home"
        run_tmp = self.work / "tmp"
        run_home.mkdir(exist_ok=True)
        run_tmp.mkdir(exist_ok=True)
        args = [
            str(self.cfg.binary),
            "-basedir", str(self.work / "base"),
            # Network sockets are created before autoexec.cfg is executed.
            # Pass the port on the command line so dedicated helper engines
            # do not all compete for the default 26000 during parallel runs.
            "-port", str(self.port),
            "-stress", str(self.script),
            "-window",
            "-width", "640", "-height", "480",
            # NSArgumentDomain: keep macOS App Nap from throttling a window that
            # sits behind the terminal, which otherwise looks exactly like a hang
            "-NSAppSleepDisabled", "YES",
            # A crash-heavy campaign makes macOS offer "reopen windows?" on the
            # next launch.  That dialog blocks startup before any engine code
            # runs, so every subsequent run looks like a boot-time hang with an
            # empty log.  Opt out of state restoration entirely.
            "-ApplePersistenceIgnoreState", "YES",
        ]
        if self.cfg.nosound:
            args += ["-nosound"]
        args += self.extra_args
        for c in boot_cmds:
            args += ["+" + c] if " " not in c else ["+" + c.split(" ", 1)[0], *c.split(" ")[1:]]
        launch_args = args
        if getattr(self.cfg, "contain", False):
            profile = sandbox_profile(self.cfg, self.work)
            launch_args = ["sandbox-exec", "-f", str(profile), *args]
        self.cmdline = launch_args
        (self.work / "cmdline.txt").write_text(" ".join(launch_args))
        if self.event_log:
            self.event_log.record("actor_start", actor=self.actor,
                                  scenario=self.scenario,
                                  port=self.port,
                                  rcon_password=getattr(self.cfg, "rcon_password", "stresspw"),
                                  boot_cmds=list(boot_cmds),
                                  extra_args=list(self.extra_args),
                                  contain=bool(getattr(self.cfg, "contain", False)),
                                  limits={
                                      "cpu": getattr(self.cfg, "cpu_limit", 300),
                                      "memory_mb": getattr(self.cfg, "memory_limit_mb", 4096),
                                      "file_mb": getattr(self.cfg, "file_limit_mb", 128),
                                      "fd": getattr(self.cfg, "fd_limit", 256),
                                      "processes": getattr(self.cfg, "process_limit", 0),
                                  })
        env = os.environ.copy()
        env.update({
            "HOME": str(run_home),
            "TMPDIR": str(run_tmp),
            "XDG_CONFIG_HOME": str(run_home / ".config"),
            "XDG_CACHE_HOME": str(run_home / ".cache"),
            "XDG_STATE_HOME": str(run_home / ".local" / "state"),
        })
        fh = open(self.logpath, "wb")
        self.proc = subprocess.Popen(launch_args, stdout=fh, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL, start_new_session=True,
                                     cwd=Path("/") if getattr(self.cfg, "contain", False) else self.work,
                                     env=env,
                                     preexec_fn=lambda: apply_child_limits(self.cfg))
        fh.close()
        self._logfh = open(self.logpath, "r", errors="replace")
        return self

    def _port_busy(self):
        return ("UDP4_OpenSocket: Address already in use" in self._logbuf or
                "UDP6_OpenSocket: Address already in use" in self._logbuf)

    def _retry_port(self):
        old_port = self.port
        runner = getattr(self.cfg, "runner", None)
        new_port = (runner.allocate_port() if runner is not None else old_port + 1)
        self.kill()
        self.set_local_endpoint(new_port,
                                getattr(self.cfg, "rcon_password", "stresspw"))
        if self.event_log:
            self.event_log.record("port_retry", actor=self.actor,
                                  old_port=old_port, port=new_port)
        self.start()

    def set_local_endpoint(self, port, password):
        """Restore the exact local endpoint captured in an event log."""
        self.port = int(port)
        self.rcon.close()
        self.rcon = Rcon(self.port, password=password)
        cfg = self.work / "base" / "id1" / "autoexec.cfg"
        if not cfg.exists():
            return
        lines = [line for line in cfg.read_text().splitlines()
                 if not line.startswith(("port ", "rcon_password "))]
        lines += [f"rcon_password {password}", f"port {self.port}"]
        cfg.write_text("\n".join(lines) + "\n")

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def returncode(self):
        return None if self.proc is None else self.proc.poll()

    def kill(self):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                try:
                    self.proc.kill()
                except ProcessLookupError:
                    pass
        if self.proc:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self.rcon.close()

    # -- command channel ---------------------------------------------------
    def send(self, *cmds, note=None):
        if not cmds:
            return
        if not self.alive():
            raise Dead(f"engine exited (rc={self.returncode()}) before: {cmds[0]}")
        self._cmdseq += 1
        text = f"@seq {self._cmdseq}\n" + "".join(c.rstrip("\n") + "\n" for c in cmds)
        with open(self.script, "a") as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        for c in cmds:
            self.journal.append(c)
        if note:
            self.journal.append("// " + note)
        if self.event_log:
            self.event_log.record("actor_command", actor=self.actor,
                                  channel="script", commands=list(cmds), note=note)

    def note_file(self, path, relative):
        """Record a generated input file so --replay can rebuild it.

        Fuzzed demos/paks are as much a part of the repro as the commands are;
        without this a finding's repro.txt replays against missing files.
        """
        store = self.work / "inputs" / relative
        store.parent.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copy2(path, store)
        except OSError:
            return
        self.journal.append(f"// file: {relative}")
        if self.event_log:
            digest = hashlib.sha256(store.read_bytes()).hexdigest()
            blob = self.work / "inputs" / "sha256" / digest
            blob.parent.mkdir(parents=True, exist_ok=True)
            if not blob.exists():
                shutil.copy2(store, blob)
            self.event_log.record("input_install", actor=self.actor,
                                  destination=relative, sha256=digest,
                                  blob=f"inputs/sha256/{digest}",
                                  size=store.stat().st_size)
            self.stress_event("asset", phase="input-install", input_sha=digest,
                              adapter=Path(relative).suffix.lstrip(".") or "raw",
                              loader_hit=None)

    def restore_file(self, relative, source_root, digest=None):
        source_root = Path(source_root)
        src = (source_root / "inputs" / "sha256" / digest
               if digest else source_root / "inputs" / relative)
        if not src.exists() and digest:
            src = source_root / "inputs" / relative
        if not src.exists():
            return False
        dst = self.work / "base" / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        return True

    def answer_dialog(self):
        """Answer a blocking SCR_ModalMessage over the priority channel.

        Not the main script: a dialog opens with the fuzzer's queued keys still
        in flight, so an answer sent down the script would sit behind that
        backlog while the pump dispatched stale keys into the dialog.
        """
        try:
            with open(str(self.script) + ".answer", "a") as f:
                f.write(f"_stress_char 110\n_stress_key {K['ESCAPE']}\n")
                f.flush()
                os.fsync(f.fileno())
        except OSError:
            pass
        self.dialogs_answered += 1

    def barrier(self, cond, note=None):
        """Hold the rest of the journal until the engine reaches a state.

        Replaces "sleep and hope": the engine itself decides when to continue,
        so a replay is deterministic and the minimizer can keep barriers while
        deleting the actions between them.
        """
        self.send(f"@barrier {cond}", note=note)

    def send_rcon(self, cmd):
        self.journal.append("// rcon: " + cmd)
        if self.event_log:
            self.event_log.record("actor_command", actor=self.actor,
                                  channel="rcon", commands=[cmd])
        return self.rcon.send(cmd)

    def stress_event(self, target, phase, **fields):
        """Emit a structured lane event without changing engine behavior."""
        if not self.event_log:
            return
        snapshot = {
            "alive": self.alive(),
            "returncode": self.returncode(),
            "cpu_seconds": round(self.cpu_seconds(), 3),
        }
        self.event_log.record(
            "STRESS_EVENT", actor=self.actor,
            scenario=getattr(self, "scenario", self.actor),
            target=target, phase=phase,
            level=getattr(self.cfg, "stress_level", "custom"),
            resource_snapshot=snapshot, **fields)

    def expect_error(self, kind):
        """Declare only the rejection class this lane treats as expected."""
        self.expected_errors.add(kind)
        if self.event_log:
            self.event_log.record("policy", actor=self.actor,
                                  expected_errors=sorted(self.expected_errors))

    # -- log ---------------------------------------------------------------
    def read_log(self):
        if self._logfh is None:
            return ""
        chunk = self._logfh.read()
        if chunk:
            self._logbuf += chunk
            self._coverage_buf += chunk
            lines = self._coverage_buf.splitlines(keepends=True)
            self._coverage_buf = ""
            if lines and not lines[-1].endswith(("\n", "\r")):
                self._coverage_buf = lines.pop()
            for line in lines:
                match = self.COVERAGE_RE.search(line)
                if match and self.coverage_sink:
                    self.coverage_sink(self, match.group(1), int(match.group(2)))
        return chunk

    def log_text(self):
        self.read_log()
        return self._logbuf

    def log_tail(self, nlines=120):
        return "\n".join(self.log_text().splitlines()[-nlines:])

    # -- status / liveness -------------------------------------------------
    STATUS_RE = re.compile(r"STRESS_STATUS ([^\n]*)")
    CAPS_RE = re.compile(r"STRESS_CAPS ([^\n]*)")
    NETSTATE_RE = re.compile(r"STRESS_NETSTATE ([^\n]*)")
    COVERAGE_RE = re.compile(r"STRESS_COVERAGE parser=(\S+) id=(\d+)")

    def cpu_seconds(self):
        """Total CPU time the engine has burned, for telling spin from stall."""
        if not self.proc:
            return 0.0
        try:
            out = subprocess.run(["ps", "-o", "time=", "-p", str(self.proc.pid)],
                                 capture_output=True, text=True, timeout=5).stdout.strip()
        except Exception:
            return 0.0
        if not out:
            return 0.0
        parts = out.split(":")
        try:
            return sum(float(p) * 60 ** i for i, p in enumerate(reversed(parts)))
        except ValueError:
            return 0.0

    def _await_status(self, seq, timeout):
        want = f"seq={seq} "
        deadline = now() + timeout
        while now() < deadline:
            self.read_log()
            for m in self.STATUS_RE.finditer(self._logbuf):
                line = m.group(1)
                if line.startswith(want):
                    st = dict(
                        (k, v) for k, v in
                        (kv.split("=", 1) for kv in line.split() if "=" in kv)
                    )
                    self.last_acked = int(st.get("acked", 0) or 0)
                    self.last_exec = int(st.get("exec", 0) or 0)
                    return st
            if not self.alive():
                raise Dead(f"engine exited (rc={self.returncode()}) awaiting status")
            time.sleep(0.05)
        return None

    def status(self, timeout=None):
        """Liveness probe.  Retries before crying hang: a backgrounded window
        can be throttled hard by the OS, which is indistinguishable from a
        wedged frame loop on the first try."""
        timeout = timeout or self.cfg.hang_timeout
        self._seq += 1
        seq = self._seq
        self.send(f"_stress_status {seq}")

        # a modal dialog is by far the most common reason a probe goes quiet;
        # answer it promptly rather than burning the first three seconds
        st = self._await_status(seq, min(0.5, timeout))
        if st is not None:
            return st
        self.answer_dialog()
        st = self._await_status(seq, max(0.0, timeout - 0.5))
        if st is not None:
            return st

        cpu0 = self.cpu_seconds()
        for attempt in range(self.cfg.hang_retries):
            self.answer_dialog()
            self._seq += 1
            seq = self._seq
            self.send(f"_stress_status {seq}")
            st = self._await_status(seq, timeout)
            if st is not None:
                self.throttle_events += 1
                return st
        cpu1 = self.cpu_seconds()

        if not self.alive():
            raise Dead(f"engine exited (rc={self.returncode()})")
        spin = "spinning" if (cpu1 - cpu0) > 0.5 else "idle (no CPU)"
        raise Hung(f"no status reply for {timeout * (1 + self.cfg.hang_retries):.0f}s, "
                   f"process {spin}; last acked={self.last_acked} exec={self.last_exec} "
                   f"(gap means the queue was barriered or starved, not that the "
                   f"frame loop was wedged)")

    def sample(self, seconds=3):
        """Grab a stack sample of a wedged engine, for triage."""
        if not self.alive():
            return None
        out = self.work / "hang-sample.txt"
        try:
            subprocess.run(["sample", str(self.proc.pid), str(seconds), "-file", str(out)],
                           capture_output=True, timeout=seconds + 30)
        except Exception:
            return None
        return out if out.exists() else None

    def wait_ready(self, timeout=90):
        """Wait for STRESS_READY + a first status reply."""
        retries = 0
        while True:
            deadline = now() + timeout
            while now() < deadline:
                self.read_log()
                if self._port_busy():
                    if retries >= self.PORT_RETRY_LIMIT:
                        raise PortBusy(
                            f"port {self.port} remained busy after "
                            f"{retries} retries")
                    retries += 1
                    self._retry_port()
                    break
                if "STRESS_READY" in self._logbuf:
                    result = self.status(timeout=max(10.0, deadline - now()))
                    self.probe_capabilities()
                    return result
                if not self.alive():
                    if self._port_busy() and retries < self.PORT_RETRY_LIMIT:
                        retries += 1
                        self._retry_port()
                        break
                    raise Dead(f"engine exited during boot (rc={self.returncode()})")
                time.sleep(0.1)
            else:
                if self._port_busy() and retries < self.PORT_RETRY_LIMIT:
                    retries += 1
                    self._retry_port()
                    continue
                raise Hung("engine never printed STRESS_READY")
            # A busy-port retry restarted the process; poll its fresh log.
            continue

    def probe_capabilities(self, timeout=3.0):
        """Ask a stress build which exact entry points it exposes.

        Older stress binaries may not implement this command, so absence is
        recorded as an unavailable capability rather than treated as a lane
        failure.  The event log still makes the distinction visible in a
        replay and in the campaign report.
        """
        if getattr(self, "capabilities", None) is not None:
            return self.capabilities
        self.capabilities = {}
        try:
            self.send("_stress_capabilities")
        except (Dead, Hung):
            return self.capabilities
        deadline = now() + timeout
        while now() < deadline:
            self.read_log()
            matches = list(self.CAPS_RE.finditer(self._logbuf))
            if matches:
                fields = matches[-1].group(1).split()
                self.capabilities = dict(
                    item.split("=", 1) for item in fields if "=" in item)
                break
            if not self.alive():
                break
            time.sleep(0.05)
        if self.event_log:
            self.event_log.record("capabilities", actor=self.actor,
                                  capabilities=dict(self.capabilities))
        return self.capabilities

    def netstate(self, timeout=5.0):
        """Read the active server socket's receive sequence state."""
        try:
            self.send("_stress_netstate")
        except (Dead, Hung):
            return None
        deadline = now() + timeout
        while now() < deadline:
            self.read_log()
            matches = list(self.NETSTATE_RE.finditer(self._logbuf))
            if matches:
                fields = dict(item.split("=", 1)
                              for item in matches[-1].group(1).split()
                              if "=" in item)
                if fields.get("state") == "no-client":
                    return None
                state = {k: int(v) for k, v in fields.items()
                         if k not in {"state"}}
                if self.event_log:
                    self.event_log.record("netstate", actor=self.actor,
                                          state=dict(state))
                return state
            if not self.alive():
                break
            time.sleep(0.05)
        return None

    def wait_spawned(self, timeout=60):
        """Wait until the client is fully in a level (signon complete).

        cls.state: 0 ca_dedicated, 1 ca_disconnected, 2 ca_connected.
        Gives up early if the load never even started -- a bad map name just
        leaves us disconnected, and there is nothing to wait for.
        """
        deadline = now() + timeout
        started = now()
        while now() < deadline:
            st = self.status()
            if st.get("signon") == "4" and st.get("state") == "2":
                return st
            if (now() - started > 4.0 and st.get("state") == "1"
                    and st.get("sv") == "0"):
                return st                      # load never took
            time.sleep(0.2)
        return self.status()

    def settle(self, seconds):
        """Let the engine run for a while, staying responsive."""
        deadline = now() + seconds
        while now() < deadline:
            time.sleep(min(0.4, max(0.05, deadline - now())))
            if not self.alive():
                raise Dead(f"engine exited (rc={self.returncode()}) while idling")
        self.status()

    # -- shutdown ----------------------------------------------------------
    def graceful_quit(self, timeout=25):
        """quit only proceeds from the console, otherwise it opens the quit menu."""
        try:
            st = self.status(timeout=10)
        except (Dead, Hung):
            return
        if st.get("keydest") != "1":
            self.send("toggleconsole")
            try:
                st = self.status(timeout=10)
            except (Dead, Hung):
                return
        self.send("quit")
        deadline = now() + timeout
        while now() < deadline:
            if not self.alive():
                return
            time.sleep(0.2)
        # the quit menu may have eaten it -- confirm with 'y'
        try:
            self.send("_stress_key 121")
        except Dead:
            return
        deadline = now() + 10
        while now() < deadline and self.alive():
            time.sleep(0.2)


def sandbox_escapes(work):
    """Files the engine wrote outside the paths a run is allowed to touch.

    A malicious server naming its own downloads is exactly the shape of bug that
    ends in a write outside the game directory, so make that loud rather than
    hoping someone notices it in a log.

    The sandbox contains copies of both PAKs and loose assets, so any symlink
    appearing during a run is itself suspicious and is reported.
    """
    work = Path(work)
    base = work / "base"
    # The engine's basedir is base/.  Everything else below is either a
    # harness artifact or an explicitly isolated process environment.  A file
    # appearing at work/evil now stands out instead of being
    # invisible because the old oracle only walked base/.
    allowed_work_files = {
        "cmdline.txt", "console.log", "journal.txt", "hang-sample.txt",
        "stress.cmds", "stress.cmds.answer", "events.jsonl", "stress.sb",
        "rcon-replies.log",
    }
    allowed_base_root = {"privkey.der", "fullchain.der", "qconsole.log", "stress.cmds"}
    bad = []
    for root, dirs, files in os.walk(work, followlinks=False):
        for dirname in dirs:
            full = Path(root) / dirname
            if full.is_symlink():
                bad.append(os.path.relpath(full, work).replace(os.sep, "/"))
        for fn in files:
            full = Path(root) / fn
            if full.is_symlink():
                bad.append(os.path.relpath(full, work).replace(os.sep, "/"))
                continue
            rel_work = os.path.relpath(full, work).replace(os.sep, "/")
            first = rel_work.split("/", 1)[0]
            if first in {"home", "tmp", "inputs"}:
                continue
            if first != "base":
                if rel_work in allowed_work_files:
                    continue
                bad.append(rel_work)
                continue
            rel_base = os.path.relpath(full, base).replace(os.sep, "/")
            if rel_base.startswith("id1/"):
                continue
            if rel_base in allowed_base_root:
                continue
            bad.append(rel_work)
    return sorted(bad)


# ---------------------------------------------------------------------------
# findings
# ---------------------------------------------------------------------------
class Finding:
    def __init__(self, kind, scenario, seed, signature, detail,
                 journal=None, log_tail="", ips=None):
        self.kind = kind
        self.scenario = scenario
        self.seed = seed
        self.signature = signature
        self.detail = detail
        self.journal = journal or []
        self.log_tail = log_tail
        self.ips = ips

    def to_dict(self):
        return {
            "kind": self.kind, "scenario": self.scenario, "seed": self.seed,
            "signature": self.signature, "detail": self.detail,
            "ips": str(self.ips) if self.ips else None,
        }


SIGNAMES = {
    signal.SIGSEGV: "SIGSEGV", signal.SIGBUS: "SIGBUS", signal.SIGABRT: "SIGABRT",
    signal.SIGILL: "SIGILL", signal.SIGFPE: "SIGFPE", signal.SIGTRAP: "SIGTRAP",
}

KNOWN_PATH = Path(__file__).with_name("known.json")


class KnownIssues:
    """Signatures we already know about, so campaigns report only new ones.

    Without this every campaign re-reports the open bugs it found last night,
    and every regression run is red for reasons nobody needs to look at.  Two
    states, and they are opposites:

      open  -- expected to still reproduce.  Seeing it again is not news; not
               seeing it any more is a hint that it was fixed.
      fixed -- expected NOT to reproduce.  Seeing it again is a regression and
               is reported loudly, whatever the campaign was doing at the time.
    """

    def __init__(self, path=None, enabled=True):
        self.path = Path(path) if path else KNOWN_PATH
        self.enabled = enabled
        self.issues = []
        if enabled and self.path.exists():
            data = json.loads(self.path.read_text())
            for entry in data.get("issues", []):
                if entry.get("signature_re"):
                    entry = dict(entry, _re=re.compile(entry["signature_re"]))
                self.issues.append(entry)

    @staticmethod
    def _entry_matches(entry, signature, kind=None, scenario=None):
        # kind/scenario are filters, not requirements: a saved FINDING.txt gives
        # us a signature and nothing else, and must still match.
        if kind and entry.get("kind") and entry["kind"] != kind:
            return False
        scenarios = entry.get("scenarios")
        if scenarios and scenario and scenario.split("/")[0] not in scenarios:
            return False
        pattern = entry.get("_re")
        if pattern is not None:
            return bool(pattern.search(signature))
        return entry.get("signature") == signature

    def match(self, finding):
        """Return the matching entry for a Finding, or None."""
        if finding is None:
            return None
        return self.match_signature(finding.signature, finding.kind, finding.scenario)

    def match_signature(self, signature, kind=None, scenario=None):
        """Signature lookup, also used for the saved FINDING.txt of a repro."""
        if not self.enabled or not signature:
            return None
        for entry in self.issues:
            if self._entry_matches(entry, signature, kind, scenario):
                return entry
        return None

    def state(self, entry):
        return (entry or {}).get("state", "open")

    def label(self, entry):
        ident = entry.get("id") or entry.get("note") or entry.get("signature", "")
        return str(ident)[:80]

    def add(self, finding, state="open", note=None):
        """Append an entry for a finding and rewrite known.json."""
        data = {"schema": 1, "issues": []}
        if self.path.exists():
            data = json.loads(self.path.read_text())
        for entry in data.get("issues", []):
            if entry.get("signature") == finding.signature:
                return None                # already known; nothing to write
        entry = {
            "id": f"QSSM-{len(data.get('issues', [])) + 1:03d}",
            "signature": finding.signature,
            "kind": finding.kind,
            "state": state,
            "scenarios": [finding.scenario.split("/")[0]] if finding.scenario else [],
            "opened": time.strftime("%Y-%m-%d"),
            "note": note or "",
        }
        data.setdefault("issues", []).append(entry)
        self.path.write_text(json.dumps(data, indent=2) + "\n")
        return entry


def classify_exit(engine, crashwatch, scenario, seed, cause):
    """Turn a dead/hung engine into a Finding, or None if it was a clean exit."""
    rc = engine.returncode()
    log = engine.log_text()
    pid = engine.proc.pid if engine.proc else None
    ips = crashwatch.poll(wait=8.0 if (rc is not None and rc < 0) else 0.5, pid=pid)
    ips = ips[-1] if ips else None

    if engine.assertion_failure:
        return Finding("assertion", scenario, seed, engine.assertion_failure,
                       f"{cause}\n{engine.assertion_failure}",
                       engine.journal, engine.log_tail())

    if ips is not None:
        info = CrashReports.summarize(ips)
        engine_frames = [f for f in info["frames"] if "QSS-M`" in f] or info["frames"]
        sig = " <- ".join(engine_frames[:3]) or "unknown"
        return Finding("crash", scenario, seed, f"{info['signal']}: {sig}",
                       f"{cause}\nsignal={info['signal']}\n" + "\n".join(info["frames"][:16]),
                       engine.journal, engine.log_tail(), ips)

    if rc is not None and rc < 0:
        signame = SIGNAMES.get(-rc, f"signal {-rc}")
        return Finding("crash", scenario, seed, f"{signame} (no report)",
                       f"{cause}\nkilled by {signame}", engine.journal, engine.log_tail())

    for pat, kind in FATAL_PATTERNS:
        m = pat.search(log)
        if m:
            text = (m.group(0) or "").strip()[:200]
            if kind == "host_error" and BENIGN_HOST_ERROR.search(text):
                continue
            if kind in engine.expected_errors:
                continue    # this lane explicitly documents the rejection policy
            severity = {"host_error": "error",
                        "parse_invariant": "invariant"}.get(kind, "fatal")
            if kind == "parse_invariant":
                # dedup on the direction, not the exact byte counts -- an
                # unguarded reader over-reads by different amounts every payload
                signature = "parse overread" if "overread" in text else "parse short-read"
            else:
                signature = f"{kind}: {text}"
            return Finding(severity, scenario, seed, signature,
                           f"{cause}\n{text}", engine.journal, engine.log_tail())

    escapes = sandbox_escapes(engine.work)
    if escapes:
        return Finding("escape", scenario, seed,
                       f"wrote outside stress sandbox: {escapes[0]}",
                       f"{cause}\nfiles written outside the allowed run paths:\n"
                       + "\n".join(escapes[:20]),
                       engine.journal, engine.log_tail())

    if isinstance(cause, str) and cause.startswith("hang"):
        return Finding("hang", scenario, seed, f"hang in {scenario}", cause,
                       engine.journal, engine.log_tail())

    if rc not in (None, 0):
        return Finding("exit", scenario, seed, f"exit code {rc} in {scenario}",
                       f"{cause}\nexit code {rc}", engine.journal, engine.log_tail())
    return None


# ---------------------------------------------------------------------------
# fuzz corpora
# ---------------------------------------------------------------------------
FUZZ_ARGS = [
    "", "0", "1", "-1", "-2", "2", "255", "256", "-255",
    "2147483647", "-2147483648", "4294967296", "99999999999999",
    "0.0", "-0.0", "1e40", "-1e40", "nan", "inf", "-inf",
    "%s%s%s%n", "%n", "%99999d",
    '"', "''", '"""', "\\", "//", ";", "$", "${x}",
    "../../../etc/passwd", "/dev/zero", "id1/../../..",
    "A" * 300, "A" * 2100, "\xc3\xa9\xc3\xa9",
    "-1 -1", "0 0 0", "-1 -1 -1 -1 -1 -1",
    "999999 999999 999999", "1 2 3 4 5 6 7 8 9 10",
    "nonexistent_thing", "*", "?", "..", ".",
]


def fuzz_arg(rng):
    if rng.random() < 0.2:
        return " ".join(rng.choice(FUZZ_ARGS) for _ in range(rng.randint(1, 4)))
    return rng.choice(FUZZ_ARGS)


def rand_text(rng, n=None):
    n = n or rng.randint(1, 24)
    pool = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
            " _-.[]{}()/\\\"';:%$#@!^&*+=<>?|~`")
    return "".join(rng.choice(pool) for _ in range(n))


def waits(n):
    """n engine frames of settling, expressed as console waits."""
    return ";".join(["wait"] * n)


# ---------------------------------------------------------------------------
# hostile wire payloads
#
# Command fuzzing only ever exercises src_command.  These payloads go the other
# way: the dedicated server writes them straight into a connected client's
# message stream, so they arrive in CL_ParseServerMessage exactly as a malicious
# server's would.  Injecting engine-side rather than forging UDP keeps the
# netchan sequencing intact, so the bytes actually reach the parser instead of
# being dropped as a framing error -- and a failing payload is a short hex
# string, which minimizes far better than a captured packet.
# ---------------------------------------------------------------------------
SVC = {
    "bad": 0, "nop": 1, "updatestat": 3, "time": 7, "stufftext": 9,
    "setangle": 10, "serverinfo": 11, "lightstyle": 12, "updatename": 13,
    "updatecolors": 17, "particle": 18, "spawnstatic": 20, "temp_entity": 23,
    "signonnum": 25, "spawnstaticsound": 29, "intermission": 30, "finale": 31,
    "cdtrack": 32, "cutscene": 34, "fog": 41, "spawnstaticsound2": 44,
    "dp_entities": 57, "fte_voicechat": 84, "fte_updateentities": 86,
}
# svc_download shares opcode 41 with svc_fog and is disambiguated by packet
# shape while a download is in flight -- a deliberately good confusion target.
SVC_DOWNLOAD = 41

# Well-formed-ish records, used as truncation and mutation seeds.
def _wire_seeds():
    import struct as _s
    z = b"\x00"
    return [
        ("time",              bytes([SVC["time"]]) + _s.pack("<f", 12.5)),
        ("setangle",          bytes([SVC["setangle"]]) + b"\x10\x20\x30"),
        ("updatestat",        bytes([SVC["updatestat"]]) + b"\x0f" + _s.pack("<i", 1234)),
        ("lightstyle",        bytes([SVC["lightstyle"]]) + b"\x02" + b"abcdefg" + z),
        ("updatename",        bytes([SVC["updatename"]]) + b"\x00" + b"player" + z),
        ("updatecolors",      bytes([SVC["updatecolors"]]) + b"\x00\x44"),
        ("particle",          bytes([SVC["particle"]]) + b"\x00\x01\x00\x02\x00\x03" + b"\x01\x02\x03\x0a\x0b"),
        ("spawnstaticsound",  bytes([SVC["spawnstaticsound"]]) + b"\x00\x01\x00\x02\x00\x03" + b"\x05\xff\x40"),
        ("spawnstaticsound2", bytes([SVC["spawnstaticsound2"]]) + b"\x00\x01\x00\x02\x00\x03" + b"\x05\x00\xff\x40"),
        ("cdtrack",           bytes([SVC["cdtrack"]]) + b"\x03\x03"),
        ("intermission",      bytes([SVC["intermission"]])),
        ("finale",            bytes([SVC["finale"]]) + b"the end" + z),
        ("cutscene",          bytes([SVC["cutscene"]]) + b"scene" + z),
        ("signonnum",         bytes([SVC["signonnum"]]) + b"\x02"),
        ("fog",               bytes([SVC["fog"]]) + b"\x80\x40\x40\x40" + _s.pack("<f", 1.0)),
        ("temp_entity",       bytes([SVC["temp_entity"]]) + b"\x00" + b"\x00\x01\x00\x02\x00\x03"),
        ("dp_entities",       bytes([SVC["dp_entities"]]) + b"\x01\x00\x00\x00"),
        ("fte_updateentities",bytes([SVC["fte_updateentities"]]) + b"\x01\x00\x00\x00"),
        ("fte_voicechat",     bytes([SVC["fte_voicechat"]]) + b"\x00\x00\x01\x00\x08" + b"\x00" * 8),
        ("serverinfo",        bytes([SVC["serverinfo"]]) + _s.pack("<i", 15) + b"\x00\x08" + b"maps/x.bsp" + z + z + z),
    ]


STUFF_PAYLOADS = [
    b"quix\n", b"alias a \"alias a quix\"; a\n", b"cmd status\n",
    b"connect 127.0.0.1:1\n", b"download ../../../etc/passwd\n",
    b"exec quake.rc\n", b"bind a \"quit\"\n", b'say "' + b"A" * 900 + b'"\n',
    b";;;;\n", b"\n" * 40, b"alias " + b"B" * 300 + b" quit\n",
    b"unbindall;bind ` toggleconsole\n", b"wait;wait;wait;quix\n",
    b"impulse " + b"9" * 200 + b"\n", b"name \x01\x02\x03\xff\n",
]

BOUNDARY_COUNTS = [0, 1, 2, 254, 255, 256, 257, 1023, 1024, 2047, 2048, 2049,
                   4095, 4096, 4097, 32767, 32768, 65535]


def wire_payload(rng, corpus=()):
    """Return (name, bytes) for one hostile server payload."""
    import struct as _s
    seeds = _wire_seeds()
    mode = rng.random()

    if corpus and mode < 0.20:
        name, data = rng.choice(corpus)
        mutant = bytearray(data)
        if mutant and rng.random() < 0.75:
            for _ in range(rng.randint(1, min(4, len(mutant)))):
                mutant[rng.randrange(len(mutant))] ^= rng.randrange(1, 256)
        if mutant and rng.random() < 0.25:
            del mutant[rng.randrange(len(mutant))]
        return (f"corpus/{name}", bytes(mutant))

    if mode < 0.30:
        # truncation: the highest-yield family.  MSG_ReadFloat/MSG_ReadDouble
        # index net_message.data without the msg_badread guard the byte/short/
        # long readers use, so cutting a record short walks off the end.
        name, rec = rng.choice(seeds)
        if len(rec) < 2:
            return (f"trunc/{name}/full", rec)
        cut = rng.randrange(1, len(rec))
        return (f"trunc/{name}/{cut}", rec[:cut])

    if mode < 0.45:
        name, rec = rng.choice(seeds)
        rec = bytearray(rec)
        for _ in range(rng.randint(1, 3)):
            rec[rng.randrange(len(rec))] = rng.randint(0, 255)
        return (f"mutate/{name}", bytes(rec))

    if mode < 0.58:
        return ("stufftext", bytes([SVC["stufftext"]]) + rng.choice(STUFF_PAYLOADS))

    if mode < 0.70:
        # boundary indices into the precache / stat / client tables
        n = rng.choice(BOUNDARY_COUNTS)
        which = rng.choice(["updatestat", "lightstyle", "updatename",
                            "updatecolors", "spawnstaticsound2"])
        if which == "updatestat":
            return (f"bound/updatestat/{n}",
                    bytes([SVC["updatestat"], n & 0xff]) + _s.pack("<i", n))
        if which == "lightstyle":
            return (f"bound/lightstyle/{n}",
                    bytes([SVC["lightstyle"], n & 0xff]) + b"aaa\x00")
        if which == "updatename":
            return (f"bound/updatename/{n}",
                    bytes([SVC["updatename"], n & 0xff]) + b"x\x00")
        if which == "updatecolors":
            return (f"bound/updatecolors/{n}",
                    bytes([SVC["updatecolors"], n & 0xff, 0x44]))
        return (f"bound/staticsound2/{n}",
                bytes([SVC["spawnstaticsound2"]]) + b"\x00\x01\x00\x02\x00\x03"
                + _s.pack("<H", n & 0xffff) + b"\xff\x40")

    if mode < 0.80:
        # svc_download / svc_fog opcode confusion, malformed chunk framing
        chunk = rng.choice([-1, 0, 1, 0x7fffffff, -0x80000000])
        body = _s.pack("<i", chunk) + bytes(rng.randint(0, 255)
                                            for _ in range(rng.randint(0, 24)))
        return (f"download/chunk{chunk}", bytes([SVC_DOWNLOAD]) + body)

    if mode < 0.90:
        # several records back to back -- exercises the dispatch loop's
        # lastcmd bookkeeping and any cross-record state
        out = b""
        picked = []
        for _ in range(rng.randint(2, 5)):
            name, rec = rng.choice(seeds)
            picked.append(name)
            out += rec
        return ("seq/" + "+".join(picked), out)

    # dumb baseline: unknown/reserved opcode plus noise
    op = rng.randint(0, 127)
    return (f"raw/op{op}",
            bytes([op]) + bytes(rng.randint(0, 255) for _ in range(rng.randint(0, 32))))


# ---------------------------------------------------------------------------
# invalid-asset corpus
#
# Maps, models and sounds are attacker-supplied in practice -- people download
# maps and mods constantly -- and each has a hand-rolled binary loader with
# counts and offsets read straight out of the file.  Seeds come from the real
# paks so mutants stay structurally plausible; pure random bytes get rejected by
# the magic check and never reach the interesting code.
# ---------------------------------------------------------------------------
def pak_entries(path):
    """(name, offset, length) for every file in a Quake .pak."""
    out = []
    with open(path, "rb") as f:
        magic, ofs, ln = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK" or ln <= 0 or ln % 64:
            return out
        f.seek(ofs)
        for _ in range(ln // 64):
            raw = f.read(64)
            if len(raw) < 64:
                break
            name = raw[:56].split(b"\0")[0].decode("latin1")
            fp, fl = struct.unpack("<ii", raw[56:64])
            out.append((name, fp, fl))
    return out


def pak_read(path, offset, length):
    with open(path, "rb") as f:
        f.seek(offset)
        return f.read(length)


# lump/header fields worth corrupting per type, as (offset, size) pairs.  These
# are the counts and offsets the loaders trust.
ASSET_SHAPES = {
    "bsp": dict(header=(0, 4 + 15 * 8)),   # version + 15 lump (ofs,len) pairs
    "mdl": dict(header=(0, 84)),           # ident..numskins etc.
    "md3": dict(header=(0, 108)),          # MD3 header fields
    "spr": dict(header=(0, 36)),
    "wav": dict(header=(0, 44)),           # RIFF/fmt/data chunk headers
    "lmp": dict(header=(0, 8)),
}


def md3_seed():
    """Small valid MD3 used as the base for deterministic malformed variants."""
    header = struct.pack(
        "<4si64s9i", b"IDP3", 15, b"qssm-md3-test", 0,
        1, 0, 1, 0, 108, 164, 164, 332)
    frame = struct.pack(
        "<3f3f3ff16s", 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,
        0.0, 0.0, 0.0, 1.0, b"frame0")
    surface = struct.pack(
        "<4s64s10i", b"IDP3", b"qssm-surface", 0,
        1, 0, 3, 1, 108, 120, 120, 144, 168)
    triangles = struct.pack("<3i", 0, 1, 2)
    texcoords = struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    vertices = b"".join(struct.pack("<3hH", x, y, 0, 0)
                         for x, y in ((0, 0), (64, 0), (0, 64)))
    data = header + frame + surface + triangles + texcoords + vertices
    assert len(data) == 332
    return data


def md3_issue_zero_surface_seed():
    """Header shape of ezQuake #1121's 14,108-byte attachment."""
    frame = struct.pack(
        "<3f3f3ff16s", 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,
        0.0, 0.0, 0.0, 1.0, b"frame0")
    data = struct.pack(
        "<4si64s9i", b"IDP3", 15, b"ammobox", 0,
        250, 0, 0, 0, 108, 14108, 14108, 14108) + frame * 250
    assert len(data) == 14108
    return data


def md3_variants(seed):
    """Return malformed MD3s covering header, surface, range, and index checks."""
    def field(data, offset, value):
        out = bytearray(data)
        out[offset:offset + 4] = struct.pack("<i", value)
        return bytes(out)

    # Header: numSurfaces=84, ofsSurfaces=100, ofsEnd=104.
    # Surface starts at 164; ofsTriangles/ofsShaders/ofsSt/ofsXyzNormals/
    # ofsEnd are relative fields at +88/+92/+96/+100/+104.
    padded = seed + (b"\0" * 8)
    yield "issue-1121-zero-surfaces", md3_issue_zero_surface_seed()
    yield "zero-surfaces", field(seed, 84, 0)
    yield "bad-surface-offset", field(seed, 100, len(seed) + 1)
    yield "bad-file-end", field(seed, 104, len(seed) + 1)
    yield "surface-does-not-advance", field(seed, 164 + 104, 0)
    yield "bad-triangle-range", field(seed, 164 + 88, 0x7fffffff)
    yield "bad-shader-range", field(seed, 164 + 92, 0x7fffffff)
    yield "bad-texcoord-range", field(seed, 164 + 96, 0x7fffffff)
    yield "bad-vertex-range", field(seed, 164 + 100, 0x7fffffff)
    yield "bad-triangle-index", field(seed, 272, 3)
    yield "truncated", seed[:272]
    # Keep accepted controls last: the engine may retain the model cache by name.
    yield "zero-tags-junk-offset", field(seed, 96, 0x7fffffff)
    yield "valid-tail-padding", field(padded, 104, len(padded))
    yield "valid-baseline", seed


def mutate_asset(rng, data, ext):
    """Return (tag, mutated bytes).  Biased toward header/count corruption."""
    buf = bytearray(data)
    if not buf:
        return "empty", bytes(buf)
    mode = rng.random()

    if mode < 0.30:
        # truncation -- loaders that read a count then walk it are most exposed
        cut = rng.choice([0, 1, 2, 4, 8, 16, 32, 64,
                          rng.randrange(1, max(2, len(buf)))])
        return f"trunc/{cut}", bytes(buf[:cut])

    if mode < 0.62:
        # corrupt a header field: counts and offsets become absurd, which is
        # exactly what an allocation or loop bound should be validating
        shape = ASSET_SHAPES.get(ext)
        span = shape["header"] if shape else (0, min(64, len(buf)))
        base, size = span
        size = min(size, max(4, len(buf) - base))
        if size >= 4:
            off = base + (rng.randrange(size // 4) * 4)
            if off + 4 <= len(buf):
                val = rng.choice([0, 1, 0x7fffffff, 0xffffffff, 0x80000000,
                                  0xfffffff0, len(buf), len(buf) * 16, 0xdeadbeef])
                buf[off:off + 4] = struct.pack("<I", val & 0xffffffff)
                return f"hdr/{off}={val:#x}", bytes(buf)
        return "hdr/noop", bytes(buf)

    if mode < 0.80:
        for _ in range(rng.randint(1, 64)):
            buf[rng.randrange(len(buf))] = rng.randint(0, 255)
        return "flip", bytes(buf)

    if mode < 0.90:
        # keep the magic, replace the body -- gets past the ident check
        keep = min(4, len(buf))
        return "bodyjunk", bytes(buf[:keep]) + bytes(
            rng.randint(0, 255) for _ in range(min(4096, len(buf) - keep)))

    # oversized: make the loader allocate/copy far more than the file holds
    return "grow", bytes(buf) + bytes(rng.randint(0, 255) for _ in range(rng.randint(1, 65536)))


# ---------------------------------------------------------------------------
# runtime introspection
# ---------------------------------------------------------------------------
def collect_list(eng, command, marker, pattern, timeout=20):
    """Run a listing command and scrape its output out of the console log."""
    begin, end = f"SM_{marker}_BEGIN", f"SM_{marker}_END"
    eng.send(f"echo {begin}", command, f"echo {end}")
    deadline = now() + timeout
    while now() < deadline:
        text = eng.log_text()
        if begin in text and end in text:
            body = text.split(begin, 1)[1].split(end, 1)[0]
            return sorted(set(pattern.findall(body)))
        if not eng.alive():
            raise Dead(f"engine exited (rc={eng.returncode()}) during {command}")
        time.sleep(0.15)
    return []


CMDLIST_RE = re.compile(r"^   (\S+)\s*$", re.M)
CVARLIST_RE = re.compile(r'^[!\s][\*\s][s\s] (\S+) "', re.M)


def get_commands(eng):
    names = collect_list(eng, "cmdlist", "CMDS", CMDLIST_RE)
    return [n for n in names
            if n not in COMMAND_BLOCKLIST
            and not n.startswith(("_stress", "+", "-", "menu_download", "vid_"))]


def get_cvars(eng):
    names = collect_list(eng, "cvarlist", "CVARS", CVARLIST_RE)
    return [n for n in names if not CVAR_BLOCKLIST_RE.match(n)]


# ---------------------------------------------------------------------------
# building blocks shared by scenarios
# ---------------------------------------------------------------------------
def maps_for(cfg):
    return SHAREWARE_MAPS + (REGISTERED_MAPS if cfg.registered else [])


def load_map(eng, rng, cfg, mapname=None, wait_spawn=True):
    mapname = mapname or rng.choice(maps_for(cfg))
    eng.send(f"map {mapname}", note=f"load {mapname}")
    state = None
    if wait_spawn:
        state = eng.wait_spawned(timeout=cfg.map_timeout)
    eng.stress_event("map-loader", phase="map-load", map=mapname,
                     loader_hit=bool(state and state.get("map") == mapname),
                     parser=None, protocol="local")
    return mapname


def press(eng, rng, keys, per_key_frames=2):
    for k in keys:
        eng.send(f"_stress_key {k}", waits(per_key_frames))


def type_text(eng, text):
    eng.send(f'_stress_char "{text}"', waits(2))


def escape_out(eng, times=4):
    for _ in range(times):
        eng.send(f"_stress_key {K['ESCAPE']}", waits(2))


# ---------------------------------------------------------------------------
# scenarios
# ---------------------------------------------------------------------------
def scen_smoke(eng, rng, cfg):
    """Boot, load a map, look around, quit.  Catches gross breakage fast."""
    eng.status()
    load_map(eng, rng, cfg, "start")
    eng.send("impulse 1", waits(5), "+forward", waits(20), "-forward")
    eng.settle(1.5)
    eng.send("disconnect")
    eng.settle(0.5)
    eng.status()


def scen_menu(eng, rng, cfg):
    """Walk the menu tree with randomized key input, from several client states."""
    menus = list(MENUS_LOCAL) + (MENUS_NET if cfg.allow_net else [])
    if rng.random() < 0.4:
        load_map(eng, rng, cfg)
    for i in range(cfg.iterations):
        m = rng.choice(menus)
        eng.send("togglemenu 1", waits(2), m, waits(3), note=f"open {m}")
        nkeys = rng.randint(4, 16)
        press(eng, rng, [rng.choice(NAV_KEYS) for _ in range(nkeys)])
        if rng.random() < 0.35:
            type_text(eng, rand_text(rng))
            press(eng, rng, [K["ENTER"] if rng.random() < 0.5 else K["ESCAPE"]])
        if rng.random() < 0.25:
            # the cmd/ctrl-K search palette
            eng.send("menu_search", waits(3))
            type_text(eng, rand_text(rng, rng.randint(1, 6)))
            press(eng, rng, [rng.choice([K["DOWN"], K["UP"], K["ENTER"], K["ESCAPE"], K["TAB"]])
                             for _ in range(rng.randint(2, 8))])
        if rng.random() < 0.2:
            # switch menus without leaving the current one first
            eng.send(rng.choice(menus), waits(2))
            press(eng, rng, [rng.choice(NAV_KEYS) for _ in range(3)])
        if rng.random() < 0.15:
            eng.send("toggleconsole", waits(2), "toggleconsole", waits(2))
        escape_out(eng, rng.randint(1, 5))
        if i % 3 == 0:
            eng.status()
        if rng.random() < 0.12:
            if rng.random() < 0.5:
                load_map(eng, rng, cfg)
            else:
                eng.send("disconnect", waits(4))
    eng.status()


def scen_mapchurn(eng, rng, cfg):
    """Level loads, level changes, aborted loads, save/load, disconnect churn."""
    for i in range(cfg.iterations):
        pick = rng.random()
        if pick < 0.55:
            load_map(eng, rng, cfg)
        elif pick < 0.7:
            # abort a load with a second load -- the interesting case
            a, b = rng.sample(maps_for(cfg), 2)
            eng.send(f"map {a}", waits(rng.randint(1, 6)), f"map {b}",
                     note="interrupted load")
            try:
                eng.wait_spawned(timeout=cfg.map_timeout)
            except Hung:
                raise
        elif pick < 0.8:
            eng.send(f"changelevel {rng.choice(maps_for(cfg))}")
            eng.wait_spawned(timeout=cfg.map_timeout)
        elif pick < 0.88:
            eng.send("restart")
            eng.wait_spawned(timeout=cfg.map_timeout)
        else:
            eng.send("disconnect", waits(6))
            eng.status()
            continue

        act = rng.random()
        if act < 0.25:
            eng.send("save stress", waits(10), "load stress")
            eng.wait_spawned(timeout=cfg.map_timeout)
        elif act < 0.4:
            eng.send("kill", waits(20))
        elif act < 0.5:
            eng.send(f"maxplayers {rng.randint(1, 16)}", waits(4),
                     f"deathmatch {rng.randint(0, 2)}", waits(2),
                     f"coop {rng.randint(0, 1)}", waits(2))
        elif act < 0.6:
            eng.send("listen 1", waits(4), "listen 0", waits(4))
        eng.settle(rng.uniform(0.3, 1.2))
    eng.status()


def scen_gameplay(eng, rng, cfg):
    """In-level play: weapons, movement, cheats, teleports, HUD churn."""
    load_map(eng, rng, cfg)
    eng.send("god 1", "notarget 1", waits(4))
    moves = ["+forward", "+back", "+moveleft", "+moveright", "+jump", "+speed",
             "+attack", "+use", "+strafe", "+lookup", "+lookdown", "+mlook"]
    for i in range(cfg.iterations):
        r = rng.random()
        if r < 0.3:
            mv = rng.choice(moves)
            eng.send(mv, waits(rng.randint(2, 25)), "-" + mv[1:])
        elif r < 0.45:
            eng.send(f"impulse {rng.randint(1, 255)}", waits(4))
        elif r < 0.55:
            eng.send(f"give {rng.choice('123456789hasclrmnp')} {rng.randint(-5, 300)}", waits(3))
        elif r < 0.62:
            eng.send("noclip", waits(6), "fly", waits(6), "noclip", waits(4))
        elif r < 0.72:
            eng.send("setpos {} {} {} {} {} 0".format(
                rng.randint(-8000, 8000), rng.randint(-8000, 8000),
                rng.randint(-8000, 8000), rng.randint(-90, 90), rng.randint(0, 360)),
                waits(6))
        elif r < 0.8:
            eng.send(f"viewsize {rng.randint(30, 130)}",
                     f"fov {rng.randint(10, 170)}",
                     f"r_wateralpha {rng.random():.2f}", waits(4))
        elif r < 0.86:
            eng.send("kill", waits(25))
        elif r < 0.92:
            eng.send(f'say {rand_text(rng)}', f'say_team {rand_text(rng)}', waits(4))
        else:
            eng.send(f"chase_active {rng.randint(0, 1)}",
                     f"r_shadows {rng.randint(0, 1)}",
                     f"scr_diag {rng.randint(0, 4)}", waits(6))
        if i % 5 == 0:
            eng.status()
    eng.send("scr_diag 0", "chase_active 0")
    eng.status()


def scen_demo(eng, rng, cfg):
    """Record, replay, seek and time demos, including truncated/garbled ones."""
    name = f"stress{rng.randint(0, 9999)}"
    load_map(eng, rng, cfg)
    eng.send(f"record {name}", waits(6))
    for _ in range(rng.randint(2, 5)):
        eng.send("+forward", waits(rng.randint(5, 30)), "-forward",
                 f"impulse {rng.randint(1, 8)}", waits(5))
    eng.send("stop", waits(10), "disconnect", waits(6))
    eng.status()

    demos = [name] + (BUILTIN_DEMOS if cfg.registered else BUILTIN_DEMOS[:1])
    for i in range(cfg.iterations):
        d = rng.choice(demos)
        eng.send(f"playdemo {d}", waits(20))
        eng.status()
        for _ in range(rng.randint(1, 5)):
            r = rng.random()
            if r < 0.5:
                eng.send(f"jumpdemo {rng.choice(['-1', '0', '1', '5', '60', '99999', '-99999', '0.5'])}",
                         waits(15))
            elif r < 0.7:
                eng.send(f"jumpdemo {rand_text(rng, 4)}", waits(8))
            elif r < 0.85:
                eng.send(f"host_timescale {rng.choice(['0', '0.1', '4', '-1', '100'])}", waits(10))
            else:
                eng.send("pause", waits(8), "pause", waits(6))
            eng.status()
        eng.send("host_timescale 1")
        if rng.random() < 0.3:
            eng.send(f"timedemo {d}", waits(40))
            eng.status(timeout=max(cfg.hang_timeout, 40))
        eng.send("disconnect", waits(6))
        eng.status()

    # replay a corrupted copy of our own recording
    src = eng.work / "base" / "id1" / "demos" / f"{name}.dem"
    if src.exists():
        data = bytearray(src.read_bytes())
        eng.expect_error("host_error")
        eng.note_file(src, f"id1/demos/{name}.dem")
        for i in range(cfg.iterations):
            mutant = bytearray(data)
            mode = rng.random()
            if mode < 0.4 and len(mutant) > 32:
                mutant = mutant[:rng.randint(8, len(mutant))]
            for _ in range(rng.randint(1, 40)):
                if not mutant:
                    break
                mutant[rng.randrange(len(mutant))] = rng.randint(0, 255)
            tag = f"stressbad{i}"
            bad = eng.work / "base" / "id1" / "demos" / f"{tag}.dem"
            bad.write_bytes(bytes(mutant))
            eng.note_file(bad, f"id1/demos/{tag}.dem")
            eng.send(f"playdemo {tag}", waits(30))
            eng.status()
            eng.send("jumpdemo 10", waits(10), "disconnect", waits(6))
            eng.status()
    eng.status()


def _fuzz_commands(eng, rng, cfg, cmds, rounds):
    for i in range(rounds):
        c = rng.choice(cmds)
        arg = fuzz_arg(rng)
        eng.send(f"{c} {arg}".strip(), waits(2))
        if i % 8 == 0:
            eng.status()


def scen_cmdfuzz(eng, rng, cfg):
    """Every console command, fuzzed args, in disconnected / spawned / demo states."""
    cmds = get_commands(eng)
    if not cmds:
        raise Hung("could not enumerate commands")
    eng.status()

    # 1. disconnected
    _fuzz_commands(eng, rng, cfg, cmds, cfg.iterations)
    # 2. mid-signon (the window that has produced real crashes before)
    for _ in range(max(3, cfg.iterations // 6)):
        eng.send(f"map {rng.choice(maps_for(cfg))}", waits(rng.randint(1, 5)))
        for _ in range(3):
            eng.send(f"{rng.choice(cmds)} {fuzz_arg(rng)}".strip(), waits(2))
        eng.wait_spawned(timeout=cfg.map_timeout)
        eng.status()
    # 3. fully spawned, with a listen server so server-side commands run too
    eng.send("listen 1", waits(6))
    _fuzz_commands(eng, rng, cfg, cmds, cfg.iterations)
    eng.status()
    # 4. during demo playback
    eng.send("disconnect", waits(4), "playdemo demo1", waits(25))
    _fuzz_commands(eng, rng, cfg, cmds, max(6, cfg.iterations // 3))
    eng.send("disconnect", waits(6))
    eng.status()


def scen_cvarfuzz(eng, rng, cfg):
    """Extreme cvar values, then force renders and level loads to use them."""
    cvars = get_cvars(eng)
    if not cvars:
        raise Hung("could not enumerate cvars")
    values = ["-1", "0", "1", "2", "999999", "-999999", "0.0001", "1e30", "-1e30",
              "nan", "inf", "", '"a b c"', "A" * 200, "0.5", "-0.5", "255", "-255"]
    for i in range(cfg.iterations):
        batch = rng.sample(cvars, min(len(cvars), rng.randint(3, 12)))
        for c in batch:
            eng.send(f"{c} {rng.choice(values)}", waits(1))
        eng.send(waits(6))
        eng.status()
        if rng.random() < 0.35:
            load_map(eng, rng, cfg)
            eng.settle(rng.uniform(0.5, 1.5))
        if rng.random() < 0.2:
            eng.send("disconnect", waits(6))
        if rng.random() < 0.25:
            eng.send("togglemenu 1", waits(3), rng.choice(MENUS_LOCAL), waits(4))
            escape_out(eng)
    eng.status()


def scen_netchurn(eng, rng, cfg):
    """Listen server + rcon command path + connect/disconnect churn."""
    load_map(eng, rng, cfg)
    eng.send("listen 1", waits(8))
    eng.status()
    for i in range(cfg.iterations):
        r = rng.random()
        if r < 0.3:
            eng.send_rcon(f"map {rng.choice(maps_for(cfg))}")
            eng.wait_spawned(timeout=cfg.map_timeout)
        elif r < 0.5:
            eng.send_rcon(rng.choice(["status", "ping", "users", "say hi",
                                      "impulse 9", "god", "noclip"]))
        elif r < 0.62:
            eng.send_rcon(f"kick {fuzz_arg(rng)}")
        elif r < 0.75:
            eng.send(f"maxplayers {rng.randint(1, 32)}", waits(4))
            eng.send(f"map {rng.choice(maps_for(cfg))}")
            eng.wait_spawned(timeout=cfg.map_timeout)
            eng.send("listen 1", waits(6))
        elif r < 0.85:
            eng.send("disconnect", waits(8))
            load_map(eng, rng, cfg)
            eng.send("listen 1", waits(6))
        else:
            eng.send(f"name {rand_text(rng)}", f"color {rng.randint(-1, 20)} {rng.randint(-1, 20)}",
                     f"rate {rng.randint(-1, 999999)}", waits(4))
        eng.status()
    eng.status()


def scen_clientserver(eng, rng, cfg):
    """A real client talking to a separate dedicated server over UDP.

    This is the only scenario that exercises the actual network parse path --
    a listen server short-circuits through loopback with no packet encoding.
    """
    srv = cfg.runner.make_engine("server")
    eng.helpers.append(srv)
    srv.extra_args = ["-dedicated", "8"]
    srv.start(boot_cmds=[f"map {rng.choice(maps_for(cfg))}"])
    srv.wait_ready(timeout=cfg.boot_timeout)
    srv.send(f"maxplayers {rng.randint(2, 8)}", "sv_public 0", waits(4))
    srv.status()

    addr = f"127.0.0.1:{srv.port}"
    for i in range(cfg.iterations):
        eng.send(f"connect {addr}", note=f"connect {addr}")
        eng.wait_spawned(timeout=cfg.map_timeout)
        st = eng.status()
        if st.get("signon") != "4":
            eng.send("disconnect", waits(6))
            srv.status()
            continue

        for _ in range(rng.randint(2, 6)):
            r = rng.random()
            if r < 0.3:
                mv = rng.choice(["+forward", "+back", "+attack", "+jump", "+moveleft"])
                eng.send(mv, waits(rng.randint(3, 20)), "-" + mv[1:])
            elif r < 0.45:
                eng.send(f"impulse {rng.randint(1, 12)}", waits(4))
            elif r < 0.6:
                eng.send(f"say {rand_text(rng)}", waits(4))
            elif r < 0.7:
                srv.send(f"changelevel {rng.choice(maps_for(cfg))}")
                eng.wait_spawned(timeout=cfg.map_timeout)
            elif r < 0.8:
                eng.send(f"name {rand_text(rng, 40)}",
                         f"color {rng.randint(-1, 20)} {rng.randint(-1, 20)}", waits(4))
            elif r < 0.9:
                eng.send(f"rate {rng.randint(-1, 999999)}",
                         f"cl_nolerp {rng.randint(0, 1)}", waits(4))
            else:
                srv.send_rcon(f"kick {rng.choice(['1', 'nan', '-1', rand_text(rng, 6)])}")
            eng.status()

        end = rng.random()
        if end < 0.4:
            eng.send("disconnect", waits(8))
        elif end < 0.6:
            srv.send(f"map {rng.choice(maps_for(cfg))}")   # yank the level out from under the client
            eng.settle(1.5)
            eng.send("disconnect", waits(8))
        elif end < 0.8:
            srv.send("kick 1", waits(8))                   # server-initiated drop
        else:
            eng.send("reconnect", waits(10))
            eng.send("disconnect", waits(6))
        eng.status()
        srv.status()

    srv.graceful_quit(timeout=15)
    eng.status()


def scen_wirefuzz(eng, rng, cfg):
    """A hostile server: crafted svc_* payloads pushed at a real connected client.

    This is the lane command fuzzing structurally cannot reach -- everything
    else drives src_command, and the harness's own server is friendly.  Here the
    dedicated server writes attacker-shaped bytes into the client's message
    stream and we watch what the parser does with them.

    Expected: graceful disconnect, Host_Error, or an ignored packet.  A crash,
    hang, sanitizer report, or a parser-invariant violation is a finding.
    """
    srv = cfg.runner.make_engine("server")
    eng.helpers.append(srv)
    srv.extra_args = ["-dedicated", "8"]
    srv.start(boot_cmds=[f"map {rng.choice(maps_for(cfg))}"])
    srv.wait_ready(timeout=cfg.boot_timeout)
    srv.send("sv_public 0", waits(4))
    srv.status()

    # the client is about to be fed garbage on purpose; refusing it is correct
    eng.expect_error("host_error")
    addr = f"127.0.0.1:{srv.port}"

    for i in range(cfg.iterations):
        st = eng.status()
        if st.get("signon") != "4":
            eng.send(f"connect {addr}", note=f"connect {addr}")
            eng.wait_spawned(timeout=cfg.map_timeout)
            st = eng.status()
            if st.get("signon") != "4":
                eng.send("disconnect", waits(6))
                continue

        name, payload = wire_payload(rng, eng.corpus_seeds("wirefuzz", rng=rng))
        chan = "reliable" if rng.random() < 0.6 else "datagram"
        payload_sha = hashlib.sha256(payload).hexdigest()
        eng.set_coverage_input(payload, "wirefuzz", payload_sha)
        eng.stress_event("CL_ParseServerMessage", phase="wirefuzz",
                         input_sha=payload_sha, parser="servermsg",
                         protocol="current", actor_target="client",
                         reject_reason=None, loader_hit=None)
        srv.send(f"_stress_inject {chan} {payload.hex()}",
                 note=f"wire {name} ({len(payload)}b {chan})")

        # give the client a few frames to receive, parse and react
        eng.send(waits(12))
        eng.status()
        eng.clear_coverage_input()

        if rng.random() < 0.25:
            # keep playing on a poisoned connection rather than only measuring
            # the instant of the parse
            mv = rng.choice(["+forward", "+attack", "+jump"])
            eng.send(mv, waits(8), "-" + mv[1:], waits(4))
            eng.status()

    srv.graceful_quit(timeout=15)
    eng.status()


def scen_remote(eng, rng, cfg):
    """Connect to a real public server, play briefly, disconnect -- repeat.

    This is a *benign* coverage lane: it exercises the real signon and server-
    message parse path against live traffic no local server produces, and it is
    a source of real signon captures to seed wirefuzz.  It never injects or
    fuzzes anything toward the remote host -- doing that to a third-party server
    would be abuse.  Off unless --remote is given.
    """
    if not cfg.remote:
        return
    for i in range(cfg.iterations):
        eng.send(f"connect {cfg.remote}", note=f"connect {cfg.remote}")
        # real servers can be slow / unreachable; don't treat that as a bug
        st = eng.wait_spawned(timeout=cfg.map_timeout)
        if st.get("signon") != "4":
            eng.send("disconnect", waits(6))
            eng.settle(rng.uniform(1.0, 2.0))
            continue
        for _ in range(rng.randint(2, 5)):
            mv = rng.choice(["+forward", "+back", "+moveleft", "+moveright",
                             "+jump", "+attack"])
            eng.send(mv, waits(rng.randint(4, 20)), "-" + mv[1:])
            eng.status()
        if rng.random() < 0.3:
            eng.send(f"say {rand_text(rng)}", waits(4))
        eng.send("disconnect", waits(8))
        eng.status()
    eng.status()


# Registered with Cmd_AddCommand_ServerCommand: reachable ONLY from server
# stufftext, and invisible to cmdfuzz because Cmd_List_f skips srctype ==
# src_server.  This is the exact blind spot that hid the tinfo OOB write.
SERVER_COMMANDS = [
    "at", "cl_downloadbegin", "cl_downloadfinished", "cl_fullpitch",
    "cl_serverextension_download", "crx_ignorethis", "csqc_progcrc",
    "csqc_progname", "csqc_progsize", "exectrigger", "fui", "fullserverinfo",
    "ignorethis", "ignorethis_crx", "init", "it", "ls", "markdemo", "paknames",
    "paks", "pingplreport", "pingplreport2", "pq_fullpitch", "reconnect",
    "svi", "tinfo", "ui", "vwep", "wps",
]

# args shaped for the download/serverinfo commands specifically, on top of the
# generic fuzz corpus
DOWNLOAD_ARGS = [
    '0 "progs/x.mdl"', '-1 "progs/x.mdl"', '99999999999 "maps/y.bsp"',
    '0 "../../../etc/passwd"', '0 "progs/\xff\xfe.mdl"',
    '0 "' + "A" * 400 + '.mdl"', '0 ""', '0', '',
    '0 "id1/../../../tmp/evil.mdl"', '0 "sound/../../x.wav"',
    '-2147483648 "progs/x.mdl"', '0 "progs/x.exe"', '0 "paks/../x.pak"',
    '1 "maps/a.bsp" extra junk here', '0 "a" "b" "c"',
]


def scen_servercmdfuzz(eng, rng, cfg):
    """Fuzz the server-only command surface, delivered as real svc_stufftext.

    These commands cannot be typed at the console and do not appear in cmdlist,
    so every other lane misses them -- yet a server can invoke all of them.
    Payloads go out as svc_stufftext from a real dedicated server so they take
    the genuine src_server dispatch path.
    """
    srv = cfg.runner.make_engine("server")
    eng.helpers.append(srv)
    srv.extra_args = ["-dedicated", "8"]
    srv.start(boot_cmds=[f"map {rng.choice(maps_for(cfg))}"])
    srv.wait_ready(timeout=cfg.boot_timeout)
    srv.send("sv_public 0", waits(4))
    srv.status()

    eng.expect_error("host_error")
    addr = f"127.0.0.1:{srv.port}"

    for i in range(cfg.iterations):
        st = eng.status()
        if st.get("signon") != "4":
            eng.send(f"connect {addr}", note=f"connect {addr}")
            eng.wait_spawned(timeout=cfg.map_timeout)
            if eng.status().get("signon") != "4":
                eng.send("disconnect", waits(6))
                continue

        cmd = rng.choice(SERVER_COMMANDS)
        if cmd.startswith(("cl_download", "cl_serverextension")):
            arg = rng.choice(DOWNLOAD_ARGS)
        elif rng.random() < 0.3:
            arg = rng.choice(DOWNLOAD_ARGS)
        else:
            arg = fuzz_arg(rng)

        line = f"{cmd} {arg}".strip()
        eng.stress_event("src_server", phase="servercmdfuzz",
                         input_sha=hashlib.sha256(line.encode()).hexdigest(),
                         parser="server-command", protocol="current",
                         actor_target="client", reject_reason=None,
                         loader_hit=None)
        # svc_stufftext + the command text + newline; the client runs it through
        # the src_server dispatch, which is the only way to reach these
        payload = bytes([SVC["stufftext"]]) + line.encode("utf-8", "surrogateescape") + b"\n\x00"
        eng.set_coverage_input(payload, "servercmdfuzz")
        srv.send(f"_stress_inject reliable {payload.hex()}",
                 note=f"servercmd {line[:70]}")
        eng.send(waits(10))
        eng.status()
        eng.clear_coverage_input()

    srv.graceful_quit(timeout=15)
    eng.status()


def scen_msgboundary(eng, rng, cfg):
    """Exercise the exact client/server message parser entry points."""
    if eng.capabilities.get("exact_servermsg") != "1":
        eng.assertion_failure = "stress binary lacks exact server-message hook"
        return

    srv = cfg.runner.make_engine("server")
    eng.helpers.append(srv)
    srv.extra_args = ["-dedicated", "8"]
    srv.start(boot_cmds=["map start"])
    srv.wait_ready(timeout=cfg.boot_timeout)
    if srv.capabilities.get("exact_clientmsg") != "1":
        eng.assertion_failure = "stress binary lacks exact client-message hook"
        return

    addr = f"127.0.0.1:{srv.port}"
    eng.send(f"connect {addr}", note=f"connect {addr}")
    eng.wait_spawned(timeout=cfg.map_timeout)
    eng.expect_error("host_error")
    srv.expect_error("host_error")

    server_record = _wire_seeds()[0][1]  # svc_time control record
    for cut in range(len(server_record) + 1):
        st = eng.status()
        if st.get("signon") != "4":
            eng.send(f"connect {addr}", note=f"reconnect {addr}")
            eng.wait_spawned(timeout=cfg.map_timeout)
        payload = server_record[:cut]
        arg = payload.hex() if payload else "-"
        eng.set_coverage_input(payload, "msgboundary")
        eng.stress_event("CL_ParseServerMessage", phase="msgboundary",
                         input_sha=hashlib.sha256(payload).hexdigest(),
                         parser="servermsg", protocol="current",
                         actor_target="client", reject_reason=None,
                         loader_hit=None)
        eng.send(f"_stress_parse_servermsg {arg}", waits(2),
                 note=f"servermsg prefix {cut}/{len(server_record)}")
        eng.status()
        eng.clear_coverage_input()

    st = eng.status()
    if st.get("signon") != "4":
        eng.send(f"connect {addr}", note=f"reconnect {addr}")
        eng.wait_spawned(timeout=cfg.map_timeout)
    for payload in (b"", bytes([1])):  # empty reject, clc_nop control
        st = eng.status()
        if st.get("signon") != "4":
            eng.send(f"connect {addr}", note=f"reconnect {addr}")
            eng.wait_spawned(timeout=cfg.map_timeout)
        arg = payload.hex() if payload else "-"
        srv.set_coverage_input(payload, "msgboundary")
        srv.stress_event("SV_ReadClientMessage", phase="msgboundary",
                         input_sha=hashlib.sha256(payload).hexdigest(),
                         parser="clientmsg", protocol="current",
                         actor_target="server", reject_reason=None,
                         loader_hit=None)
        srv.send(f"_stress_parse_clientmsg {arg}", waits(2),
                 note=f"clientmsg {len(payload)} bytes")
        srv.status()
        srv.clear_coverage_input()

    srv.graceful_quit(timeout=15)
    eng.status()


def scen_clientwirefuzz(eng, rng, cfg):
    """Replay raw client-shaped UDP frames at the server receive boundary."""
    import struct as _s

    if eng.capabilities.get("raw_datagram") != "1":
        eng.assertion_failure = "stress binary lacks raw datagram hook"
        return

    srv = cfg.runner.make_engine("server")
    eng.helpers.append(srv)
    srv.extra_args = ["-dedicated", "8"]
    srv.start(boot_cmds=["map start"])
    srv.wait_ready(timeout=cfg.boot_timeout)
    if srv.capabilities.get("raw_datagram") != "1":
        eng.assertion_failure = "stress binary lacks raw datagram hook on server"
        return

    addr = f"127.0.0.1:{srv.port}"
    eng.send(f"connect {addr}", note=f"connect {addr}")
    eng.wait_spawned(timeout=cfg.map_timeout)

    def control(payload, declared=None):
        length = 4 + len(payload) if declared is None else declared
        return _s.pack("!I", 0x80000000 | length) + payload

    def netchan(payload, sequence=0, declared=None, flags=0x00100000):
        length = 8 + len(payload) if declared is None else declared
        return _s.pack("!II", flags | length, sequence) + payload

    packets = [
        ("control-valid-server-info", control(bytes([2]) + b"QUAKE\0")),
        ("control-bad-declared-length", control(bytes([2]) + b"QUAKE\0", 0xffff)),
        ("control-unknown-command", control(b"\x7fnoise\0")),
        ("netchan-bad-declared-length", netchan(b"\x01", declared=0xffff)),
        ("netchan-unknown-flags", netchan(b"\x01", flags=0x00400000)),
    ]
    state = srv.netstate()
    if not state:
        raise Hung("server did not expose an active netchan state")
    packets.append(("netchan-valid-reliable",
                    netchan(b"\x01", sequence=state["recvseq"],
                            flags=0x00090000)))  # DATA|EOM, clc_nop
    state = srv.netstate()
    if not state:
        raise Hung("server lost the active netchan state")
    # The live client may advance this sequence before the scripted replay is
    # serviced. Keep this as a deliberate stale-sequence case: it exercises
    # the unreliable receive policy without claiming a timing-sensitive pass.
    packets.append(("netchan-stale-unreliable",
                    netchan(b"\x01", sequence=state["unreliableseq"])))
    for name, packet in srv.corpus_seeds("clientwirefuzz", limit=4, rng=rng):
        if 4 <= len(packet) <= 8192:
            packets.append((f"corpus/{name}", packet))
    for tag, packet in packets:
        digest = hashlib.sha256(packet).hexdigest()
        srv.set_coverage_input(packet, "clientwirefuzz", digest)
        srv.stress_event("connectionless_and_netchan_datagram_boundary",
                         phase="clientwirefuzz", input_sha=digest,
                         parser="raw-datagram", protocol="current",
                         actor_target="server", reject_reason=None,
                         loader_hit=None, adapter=tag)
        srv.send(f"_stress_replay_datagram {packet.hex()}",
                 note=f"raw client datagram {tag} ({len(packet)}b)")
        srv.status()
        srv.clear_coverage_input()
        eng.status()

    srv.graceful_quit(timeout=15)
    eng.status()


def scen_protocolmatrix(eng, rng, cfg):
    """Complete a real local signon under each supported server protocol."""
    profiles = (
        ("netquake15", "15"),
        ("fitzquake666", "666"),
        ("rmq999", "999"),
        ("bjp3-10002", "10002"),
    )

    for name, protocol in profiles:
        srv = cfg.runner.make_engine("server-" + name)
        eng.helpers.append(srv)
        srv.extra_args = ["-dedicated", "8"]
        srv.start(boot_cmds=[f"sv_protocol {protocol}", "map start"])
        srv.wait_ready(timeout=cfg.boot_timeout)
        if srv.capabilities.get("qssm_stress") != "1":
            eng.assertion_failure = f"protocol {protocol} server lacks stress hook"
            return

        address = f"127.0.0.1:{srv.port}"
        digest = hashlib.sha256(protocol.encode("ascii")).hexdigest()
        eng.stress_event("protocol_signon", phase="protocolmatrix",
                         input_sha=digest, parser="protocol-negotiation",
                         protocol=protocol, actor_target="client",
                         reject_reason=None, loader_hit=None,
                         profile=name)
        eng.send(f"connect {address}", note=f"{name} connect {address}")
        status = eng.wait_spawned(timeout=cfg.map_timeout)
        signon_ok = status.get("signon") == "4"
        srv.set_coverage_input(protocol.encode("ascii"), "protocolmatrix")
        server_state = srv.netstate()
        server_protocol = (str(server_state["protocol"])
                           if server_state else None)
        profile_ok = signon_ok and server_protocol == protocol
        eng.stress_event("protocol_signon_result", phase="protocolmatrix",
                         input_sha=digest, parser="protocol-negotiation",
                         protocol=protocol, actor_target="client",
                         reject_reason=None if profile_ok else
                         ("signon-incomplete" if not signon_ok else
                          "server-protocol-mismatch"),
                         loader_hit=profile_ok, profile=name,
                         signon=status.get("signon"),
                         server_protocol=server_protocol,
                         server_protocolflags=(server_state["protocolflags"]
                                               if server_state else None))
        if not profile_ok and eng.assertion_failure is None:
            eng.assertion_failure = (
                f"protocol profile {protocol} ({name}) failed: "
                f"signon={status.get('signon')} server_protocol={server_protocol}"
            )

        eng.send("disconnect", waits(3), note=f"disconnect {name}")
        eng.status()
        srv.graceful_quit(timeout=15)
        eng.status()


def scen_corpus_replay(eng, rng, cfg):
    """Replay one saved novelty input through its original harness boundary."""
    case = getattr(cfg, "corpus_case", None)
    if not case:
        eng.assertion_failure = "corpus replay missing case metadata"
        return
    adapter = case["adapter"]
    data = Path(case["blob"]).read_bytes()
    digest = case["sha256"]
    eng.stress_event("corpus_replay", phase="corpus-regress",
                     input_sha=digest, parser=case["coverage"]["parser"],
                     protocol="saved", actor_target="client",
                     reject_reason=None, loader_hit=None, adapter=adapter)

    if adapter == "protocolmatrix":
        protocol = data.decode("ascii", "strict")
        if protocol not in {"15", "666", "999", "10002"}:
            eng.assertion_failure = f"unsupported saved protocol profile {protocol!r}"
            return
        srv = cfg.runner.make_engine("server-corpus-protocol")
        eng.helpers.append(srv)
        srv.extra_args = ["-dedicated", "8"]
        srv.start(boot_cmds=[f"sv_protocol {protocol}", "map start"])
        srv.wait_ready(timeout=cfg.boot_timeout)
        address = f"127.0.0.1:{srv.port}"
        eng.send(f"connect {address}", note=f"corpus protocol {protocol}")
        status = eng.wait_spawned(timeout=cfg.map_timeout)
        srv.set_coverage_input(data, adapter, digest)
        server_state = srv.netstate()
        srv.clear_coverage_input()
        if status.get("signon") != "4" or not server_state:
            eng.assertion_failure = f"saved protocol {protocol} did not complete signon"
        srv.graceful_quit(timeout=15)
        return

    srv = cfg.runner.make_engine("server-corpus")
    eng.helpers.append(srv)
    srv.extra_args = ["-dedicated", "8"]
    srv.start(boot_cmds=["map start"])
    srv.wait_ready(timeout=cfg.boot_timeout)
    address = f"127.0.0.1:{srv.port}"
    eng.send(f"connect {address}", note=f"corpus {adapter} connect")
    eng.wait_spawned(timeout=cfg.map_timeout)

    if adapter in {"wirefuzz", "servercmdfuzz"}:
        eng.expect_error("host_error")
        eng.set_coverage_input(data, adapter, digest)
        srv.send(f"_stress_inject reliable {data.hex()}",
                 note=f"corpus replay {adapter} ({len(data)}b)")
        eng.send(waits(12))
        eng.clear_coverage_input()
    elif adapter == "msgboundary":
        eng.expect_error("host_error")
        arg = data.hex() if data else "-"
        if case["coverage"]["parser"] == "clientmsg":
            srv.expect_error("host_error")
            srv.set_coverage_input(data, adapter, digest)
            srv.send(f"_stress_parse_clientmsg {arg}", waits(3),
                     note=f"corpus replay {adapter} ({len(data)}b)")
            srv.status()
            srv.clear_coverage_input()
        else:
            eng.set_coverage_input(data, adapter, digest)
            eng.send(f"_stress_parse_servermsg {arg}", waits(3),
                     note=f"corpus replay {adapter} ({len(data)}b)")
            eng.clear_coverage_input()
    elif adapter == "clientwirefuzz":
        srv.set_coverage_input(data, adapter, digest)
        srv.send(f"_stress_replay_datagram {data.hex()}",
                 note=f"corpus replay {adapter} ({len(data)}b)")
        srv.status()
        srv.clear_coverage_input()
        eng.status()
    else:
        eng.assertion_failure = f"unsupported corpus adapter {adapter!r}"

    srv.graceful_quit(timeout=15)
    eng.status()


def scen_filefuzz(eng, rng, cfg):
    """Feed the engine malformed maps, models, sounds and lightmaps.

    Seeds are pulled out of the real paks and mutated, then written as loose
    files (which take precedence over the pak copies), so each mutant is loaded
    by the genuine loader through the genuine path.  Every asset type here is
    something a user downloads: maps, mod models, replacement sounds.

    Expected: a rejection message or Host_Error.  A crash, a hang, or a
    filesystem escape is a finding.
    """
    id1 = eng.work / "base" / "id1"
    pak = cfg.paks[0]
    entries = pak_entries(pak)
    if not entries:
        raise Hung("could not read seed pak")

    # what we can trigger a load for, and how
    wanted = {
        "bsp": [e for e in entries if e[0].startswith("maps/") and e[0].endswith(".bsp")],
        "mdl": [e for e in entries if e[0].endswith(".mdl")],
        "spr": [e for e in entries if e[0].endswith(".spr")],
        "wav": [e for e in entries if e[0].endswith(".wav")],
        "lmp": [e for e in entries if e[0].endswith(".lmp")],
    }
    # models/sounds only get loaded if something precaches them; these are
    # referenced by the vanilla start map, so "map start" pulls them in
    PRECACHED = ("progs/player.mdl", "progs/s_explod.spr", "progs/quaddama.mdl",
                 "progs/armor.mdl", "progs/g_shot.mdl", "progs/flame2.mdl",
                 "sound/ambience/water1.wav", "sound/ambience/wind2.wav")

    eng.expect_error("host_error")

    for i in range(cfg.iterations):
        kind = rng.choice([k for k, v in wanted.items() if v])
        pool = wanted[kind]
        if kind in ("mdl", "spr", "wav"):
            pref = [e for e in pool if e[0] in PRECACHED]
            name, ofs, ln = rng.choice(pref or pool)
        else:
            name, ofs, ln = rng.choice(pool)
        if ln <= 0 or ln > 8 * 1024 * 1024:
            continue

        data = pak_read(pak, ofs, ln)
        tag, mutant = mutate_asset(rng, data, kind)
        input_sha = hashlib.sha256(mutant).hexdigest()

        dst = id1 / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(mutant)
        eng.note_file(dst, f"id1/{name}")

        if kind == "bsp":
            mapname = name[len("maps/"):-len(".bsp")]
            eng.send(f"map {mapname}", note=f"asset {name} {tag} ({len(mutant)}b)")
            state = eng.wait_spawned(timeout=cfg.map_timeout)
        else:
            # the server precache runs synchronously during spawn, so the
            # loader executes even when signon never completes
            eng.send("map start", note=f"asset {name} {tag} ({len(mutant)}b)")
            state = eng.wait_spawned(timeout=cfg.map_timeout)
            eng.send("mcache", waits(6))     # oracle: 0x0 means it was rejected

        eng.stress_event(f"loader:{kind}", phase="filefuzz",
                         input_sha=input_sha, loader_hit=bool(state and state.get("map")),
                         parser=None, protocol="local", reject_reason=None,
                         adapter=kind)

        eng.status()
        eng.send("disconnect", waits(8))
        eng.status()

        try:
            dst.unlink()                      # restore the pak copy for next round
        except OSError:
            pass

    eng.status()


def scen_md3fuzz(eng, rng, cfg):
    """Load malformed MD3 variants and assert the valid control behaves normally."""
    id1 = eng.work / "base" / "id1"
    dst = id1 / "progs/player.mdl"
    dst.parent.mkdir(parents=True, exist_ok=True)

    for tag, mutant in md3_variants(md3_seed()):
        dst.write_bytes(mutant)
        eng.note_file(dst, f"id1/progs/player.mdl ({tag})")
        log_before = eng.log_text()
        eng.send("map start", note=f"md3 {tag} ({len(mutant)}b)")
        eng.wait_spawned(timeout=cfg.map_timeout)
        eng.send("mcache", waits(6))
        eng.status()
        eng.send("disconnect", waits(8))
        eng.status()

        log_after = eng.log_text()
        log_delta = log_after[len(log_before):] if log_after.startswith(log_before) else log_after
        rejected = "Warning: Mod_LoadMD3Model:" in log_delta
        should_reject = tag not in {"zero-tags-junk-offset", "valid-tail-padding", "valid-baseline"}
        if rejected != should_reject and eng.assertion_failure is None:
            outcome = "rejected" if rejected else "accepted"
            expected = "rejection" if should_reject else "acceptance"
            eng.assertion_failure = f"MD3 {tag}: observed {outcome}, expected {expected}"
            eng.journal.append(f"// ASSERTION FAILED: {eng.assertion_failure}")

    try:
        dst.unlink()
    except OSError:
        pass


def scen_chaos(eng, rng, cfg):
    """Randomly interleave everything else, at lower per-scenario depth."""
    parts = [scen_menu, scen_mapchurn, scen_gameplay, scen_demo, scen_cvarfuzz]
    sub = cfg.clone(iterations=max(2, cfg.iterations // 4))
    for _ in range(4):
        fn = rng.choice(parts)
        fn(eng, rng, sub)
        eng.status()


SCENARIOS = {
    "smoke": scen_smoke,
    "menu": scen_menu,
    "mapchurn": scen_mapchurn,
    "gameplay": scen_gameplay,
    "demo": scen_demo,
    "cmdfuzz": scen_cmdfuzz,
    "cvarfuzz": scen_cvarfuzz,
    "netchurn": scen_netchurn,
    "clientserver": scen_clientserver,
    "md3fuzz": scen_md3fuzz,
    "wirefuzz": scen_wirefuzz,
    "servercmdfuzz": scen_servercmdfuzz,
    "msgboundary": scen_msgboundary,
    "clientwirefuzz": scen_clientwirefuzz,
    "protocolmatrix": scen_protocolmatrix,
    "corpus": scen_corpus_replay,
    "filefuzz": scen_filefuzz,
    "remote": scen_remote,
    "chaos": scen_chaos,
}

LANE_MANIFEST_PATH = Path(__file__).with_name("lanes.json")
LANE_MANIFEST = json.loads(LANE_MANIFEST_PATH.read_text())
LANES = {lane["name"]: lane for lane in LANE_MANIFEST["lanes"]}


def _check_lane_manifest(lanes, scenarios):
    """The manifest and the code must agree in *both* directions.

    Checking only "every scenario has a lane" lets the manifest claim coverage
    that does not exist: a lane marked implemented with no scen_ function reads
    as a working lane in the manifest, in --list, and in any report built from
    it.  A lane is therefore either implemented (status implemented*, scenario
    present) or planned (status planned, scenario absent); nothing else.
    """
    problems = []
    for name in scenarios:
        lane = lanes.get(name)
        if lane is None:
            problems.append(f"scenario missing from lane manifest: {name}")
        elif not lane.get("status", "").startswith("implemented"):
            problems.append(
                f"lane {name} has a scenario but status {lane.get('status')!r};"
                " use an implemented* status")
    for name, lane in lanes.items():
        status = lane.get("status", "")
        if status.startswith("implemented") and name not in scenarios:
            problems.append(
                f"lane {name} is marked {status!r} but has no scenario;"
                " implement it or set status to \"planned\"")
        elif not status.startswith("implemented") and status != "planned":
            problems.append(f"lane {name} has unknown status {status!r}")
    if problems:
        raise RuntimeError("lanes.json disagrees with the implemented scenarios:\n  "
                           + "\n  ".join(problems))


_check_lane_manifest(LANES, SCENARIOS)
PLANNED_LANES = [name for name, lane in LANES.items()
                 if not lane.get("status", "").startswith("implemented")]

DEFAULT_STRESS_SCENARIOS = [
    "menu", "mapchurn", "gameplay", "demo", "cmdfuzz", "cvarfuzz",
    "netchurn", "clientserver", "md3fuzz", "wirefuzz", "servercmdfuzz",
    "clientwirefuzz", "protocolmatrix",
    "filefuzz", "chaos",
]

# Levels change the schedule as well as depth: higher levels repeat the full
# scenario rotation and use round-robin ordering so a long run cannot spend its
# entire budget in one randomly selected lane.  The old names remain aliases
# for scripts already using smoke/standard/deep/soak.
STRESS_LEVELS = {
    "L0": dict(scenarios=["smoke"], iterations=2, passes=1,
               round_robin=True, minutes=0, sound=False),
    "L1": dict(scenarios=["menu", "gameplay", "demo", "netchurn", "clientserver"],
               iterations=6, passes=1, round_robin=True, minutes=0, sound=True),
    "L2": dict(scenarios=["cmdfuzz", "cvarfuzz", "md3fuzz", "filefuzz",
                          "wirefuzz", "servercmdfuzz", "clientwirefuzz",
                          "protocolmatrix"],
               iterations=8, passes=1, round_robin=True, minutes=0, sound=True),
    "L3": dict(scenarios=["mapchurn", "gameplay", "demo", "netchurn", "clientserver"],
               iterations=12, passes=1, round_robin=True, minutes=0, sound=True),
    "L4": dict(scenarios=DEFAULT_STRESS_SCENARIOS, iterations=24, passes=1,
               round_robin=True, minutes=0, sound=True),
    "L5": dict(scenarios=DEFAULT_STRESS_SCENARIOS, iterations=48, passes=0,
               round_robin=True, minutes=30, sound=True),
}
STRESS_LEVELS.update({
    "smoke": STRESS_LEVELS["L0"],
    "standard": STRESS_LEVELS["L1"],
    "deep": STRESS_LEVELS["L4"],
    "soak": STRESS_LEVELS["L5"],
})


# ---------------------------------------------------------------------------
# configuration
# ---------------------------------------------------------------------------
class Config:
    def __init__(self, **kw):
        self.__dict__.update(kw)

    def clone(self, **kw):
        d = dict(self.__dict__)
        d.update(kw)
        return Config(**d)


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------
class Runner:
    def __init__(self, cfg):
        self.cfg = cfg
        self.results = Path(cfg.results).expanduser().resolve()
        self.results.mkdir(parents=True, exist_ok=True)
        self.corpus = Path(getattr(cfg, "corpus", self.results / "corpus")).expanduser().resolve()
        self.corpus.mkdir(parents=True, exist_ok=True)
        self.cfg.corpus = self.corpus
        self.cfg.coverage_sink = self.record_coverage
        self.lock = CampaignLock(self.results / ".campaign.lock")
        self.lock.acquire()
        atexit.register(self.close)
        self.crashwatch = CrashReports()
        self.known = getattr(cfg, "known", None) or KnownIssues(enabled=False)
        self.findings = []
        self.known_findings = []
        self.seen_signatures = {}
        self.run_index = 0
        self.port_cursor = 0
        self.runs_done = 0
        self.replay_source = None
        self.regression_total = 0
        self.regression_results = []
        self.stale_known = []
        self.active_event_log = None
        self.active_scenario = None
        self.coverage_ids = set()
        self.coverage_new = 0
        self.corpus_new = 0
        self.corpus_quarantined = 0
        self.corpus_hashes = {
            path.stem for path in self.corpus.glob("*/*.bin")
        }

    def close(self):
        self.lock.release()

    def record_coverage(self, engine, parser, ident):
        """Record one engine coverage edge and save the input that found it."""
        key = (parser, int(ident))
        is_new = key not in self.coverage_ids
        if is_new:
            self.coverage_ids.add(key)
            self.coverage_new += 1

        corpus_path = None
        candidate = engine.coverage_input
        if is_new and candidate and candidate["sha256"] not in self.corpus_hashes:
            adapter = re.sub(r"[^A-Za-z0-9_.-]+", "_", candidate["adapter"])
            bucket = self.corpus / (adapter or "raw")
            bucket.mkdir(parents=True, exist_ok=True)
            blob = bucket / f"{candidate['sha256']}.bin"
            meta = bucket / f"{candidate['sha256']}.json"
            try:
                blob.write_bytes(candidate["data"])
                meta.write_text(json.dumps({
                    "sha256": candidate["sha256"],
                    "adapter": candidate["adapter"],
                    "coverage": {"parser": parser, "id": int(ident)},
                    "association": "pending-input",
                    "actor": engine.actor,
                }, indent=2, sort_keys=True) + "\n")
                self.corpus_hashes.add(candidate["sha256"])
                self.corpus_new += 1
                corpus_path = str(blob.relative_to(self.corpus))
            except OSError:
                corpus_path = None

        if engine.event_log:
            engine.event_log.record(
                "coverage", actor=engine.actor, parser=parser,
                id=int(ident), new=is_new, corpus=corpus_path)
        if is_new and candidate:
            engine.clear_coverage_input()

    def corpus_entries(self):
        """Return valid, content-addressed corpus entries with their metadata."""
        entries = []
        for meta in sorted(self.corpus.glob("*/*.json")):
            blob = meta.with_suffix(".bin")
            try:
                info = json.loads(meta.read_text())
                data = blob.read_bytes()
                digest = hashlib.sha256(data).hexdigest()
                coverage = info["coverage"]
                adapter = info["adapter"]
                if digest != info["sha256"] or meta.stem != digest:
                    continue
                entries.append({
                    "adapter": adapter,
                    "blob": str(blob),
                    "sha256": digest,
                    "coverage": {
                        "parser": coverage["parser"],
                        "id": int(coverage["id"]),
                    },
                    "association": info.get("association", "legacy"),
                })
            except (OSError, ValueError, KeyError, TypeError):
                continue
        return entries

    def compact_corpus(self):
        """Quarantine duplicate coverage representatives without deleting them."""
        kept = set()
        quarantined = 0
        for entry in self.corpus_entries():
            key = (entry["coverage"]["parser"], entry["coverage"]["id"])
            if key not in kept:
                kept.add(key)
                continue
            source_blob = Path(entry["blob"])
            source_meta = source_blob.with_suffix(".json")
            target_dir = self.corpus / "quarantine" / entry["adapter"]
            target_dir.mkdir(parents=True, exist_ok=True)
            try:
                shutil.move(str(source_blob), str(target_dir / source_blob.name))
                if source_meta.exists():
                    shutil.move(str(source_meta), str(target_dir / source_meta.name))
                quarantined += 1
            except OSError:
                pass
        self.corpus_quarantined = getattr(self, "corpus_quarantined", 0) + quarantined
        return quarantined

    def corpus_regress(self):
        """Replay every saved corpus representative through its original lane."""
        entries = self.corpus_entries()
        self.regression_total = len(entries)
        self.regression_results = []
        failures = []
        print(f"corpus regression: {len(entries)} inputs under {self.corpus}", flush=True)
        for index, entry in enumerate(entries):
            self.cfg.corpus_case = entry
            before = set(self.coverage_ids)
            seed = int(entry["sha256"][:8], 16)
            finding = self.run_one("corpus", seed)
            key = (entry["coverage"]["parser"], entry["coverage"]["id"])
            covered = key in self.coverage_ids or key in before
            rel = Path(entry["blob"]).relative_to(self.corpus)
            legacy = entry.get("association") == "legacy"
            ok = finding is None and (covered or legacy)
            if covered:
                detail = "coverage replayed"
            elif legacy:
                detail = "legacy metadata; input replayed"
            else:
                detail = "coverage ID not observed"
            if not ok:
                failures.append(entry)
            self.regression_results.append((str(rel), ok, detail))
            print(f"  {'PASS' if ok else 'FAIL'} {rel} ({detail})", flush=True)
        self.cfg.corpus_case = None
        return failures

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def allocate_port(self):
        """Allocate a never-reused port within this worker's range."""
        port = self.cfg.port_base + self.port_cursor
        self.port_cursor += 1
        if port > 65535:
            raise RuntimeError("stress port range exhausted")
        return port

    def _port(self):
        return self.allocate_port()

    def make_engine(self, tag, actor=None):
        self.run_index += 1
        work = self.results / f"{self.run_index:04d}-{tag}"
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        port = self._port()
        # Keep the port in autoexec too: the command-line value covers early
        # network initialization, while this cvar keeps later restarts and
        # replayed configurations on the same endpoint.
        extra = [f"rcon_password {self.cfg.rcon_password}", f"port {port}"]
        if not self.cfg.allow_net:
            extra += OFFLINE_CFG
        Sandbox(work / "base", self.cfg.paks, autoexec_extra=extra,
                loose_root=getattr(self.cfg, "loose_root", None)).build()
        if actor is None:
            actor = "server" if tag == "server" else "client"
        engine = Engine(self.cfg, work, port, actor=actor,
                        event_log=self.active_event_log)
        engine.scenario = getattr(self, "active_scenario", tag)
        return engine

    def run_one(self, name, seed):
        fn = SCENARIOS[name]
        rng = random.Random(seed)
        eng = self.make_engine(name)
        eng.scenario = name
        self.active_scenario = name
        self.active_event_log = EventLog(eng.work / "events.jsonl")
        eng.event_log = self.active_event_log
        self.cfg.runner = self
        print(f"[run {self.run_index:04d}] {name} seed={seed} port={eng.port}", flush=True)
        finding = None
        cause = None
        try:
            eng.start()
            eng.wait_ready(timeout=self.cfg.boot_timeout)
            eng.stress_event("scenario", phase="L0", loader_hit=None,
                             parser=None, protocol="local")
            fn(eng, rng, self.cfg)
            eng.status()
        except Dead as exc:
            cause = f"died: {exc}"
        except Hung as exc:
            cause = f"hang: {exc}"
            sample = eng.sample()
            if sample:
                cause += f"\nstack sample: {sample}"
        except Exception as exc:                      # harness bug, not engine bug
            cause = f"harness error: {exc!r}"
        finally:
            if cause is None:
                eng.graceful_quit()
                if eng.alive():
                    cause = "hang: engine did not exit after quit"
            (eng.work / "journal.txt").write_text("\n".join(eng.journal) + "\n")
            for helper in eng.helpers:
                helper.read_log()
                (helper.work / "journal.txt").write_text("\n".join(helper.journal) + "\n")
                hf = classify_exit(helper, self.crashwatch, name + "/server", seed,
                                   cause or "clean")
                if hf is not None:
                    self.record(hf, helper)
                helper.kill()
            if cause is not None or self.cfg.always_check:
                finding = classify_exit(eng, self.crashwatch, name, seed, cause or "clean")
            eng.kill()
            eng.read_log()
            self.active_event_log = None
            self.active_scenario = None

        self.runs_done += 1
        if cause and cause.startswith("harness error"):
            print(f"    !! {cause}", flush=True)
        if finding:
            self.record(finding, eng)
        elif cause:
            print(f"    (unclassified: {cause})", flush=True)
        return finding

    def record(self, finding, eng):
        key = finding.signature
        n = self.seen_signatures.get(key, 0)
        self.seen_signatures[key] = n + 1
        if n:
            print(f"    [{finding.kind}] {key}  (dup #{n + 1})", flush=True)
            return

        entry = self.known.match(finding)
        known_open = entry is not None and self.known.state(entry) == "open"
        if entry is not None and not known_open:
            # marked fixed, and it came back
            finding.regression_of = entry
            finding.signature = f"REGRESSION [{self.known.label(entry)}]: {key}"

        (self.known_findings if known_open else self.findings).append(finding)
        d = eng.work
        known_line = ""
        if entry is not None:
            known_line = (f"known: {self.known.label(entry)} "
                          f"(state={self.known.state(entry)})\n")
        (d / "FINDING.txt").write_text(
            f"kind: {finding.kind}\nscenario: {finding.scenario}\nseed: {finding.seed}\n"
            f"signature: {finding.signature}\n{known_line}\n{finding.detail}\n\n"
            f"--- log tail ---\n{finding.log_tail}\n")
        (d / "repro.txt").write_text("\n".join(finding.journal) + "\n")
        event_path = getattr(eng.event_log, "path", None)
        if event_path and Path(event_path).exists():
            shutil.copy2(event_path, d / "repro.jsonl")
        if finding.ips:
            try:
                shutil.copy2(finding.ips, d / finding.ips.name)
            except OSError:
                pass
        if known_open:
            print(f"    [known {self.known.label(entry)}] {finding.kind}: {key}\n"
                  f"        artifacts: {d}", flush=True)
        else:
            print(f"    *** {finding.kind.upper()}: {finding.signature}\n"
                  f"        artifacts: {d}", flush=True)

    def campaign(self):
        cfg = self.cfg
        names = cfg.scenarios
        deadline = now() + cfg.minutes * 60 if cfg.minutes else None
        rng = random.Random(cfg.seed)
        i = 0
        while True:
            if deadline and now() >= deadline:
                break
            if cfg.runs and i >= cfg.runs:
                break
            name = names[i % len(names)] if cfg.round_robin else rng.choice(names)
            self.run_one(name, rng.randrange(1 << 30))
            i += 1
        return self.findings

    # -- replay / minimize -------------------------------------------------
    def replay(self, lines, tag="replay", pace=None, source_root=None):
        if lines and isinstance(lines[0], dict):
            return self.replay_events(lines, tag=tag, pace=pace, source_root=source_root)
        return self._replay_text(lines, tag=tag, pace=pace, source_root=source_root)

    def _replay_text(self, lines, tag="replay", pace=None, source_root=None):
        """Re-run a recorded journal.  Returns (reproduced, finding)."""
        pace = self.cfg.replay_pace if pace is None else pace
        self.active_event_log = None
        eng = self.make_engine(tag)
        cause = None
        source_root = source_root or self.replay_source
        try:
            for line in lines:
                if line.strip().startswith("// file: ") and source_root:
                    rel = line.strip()[len("// file: "):]
                    if eng.restore_file(rel, source_root):
                        eng.fed_corrupt_input = True
                    else:
                        print(f"    warning: repro input missing: {rel}", flush=True)
            eng.start()
            eng.wait_ready(timeout=self.cfg.boot_timeout)
            for line in lines:
                line = line.strip()
                if not line:
                    continue
                if line.startswith("// rcon: "):
                    eng.send_rcon(line[len("// rcon: "):])
                    time.sleep(pace)
                    continue
                if line.startswith("//"):
                    continue
                eng.send(line)
                time.sleep(pace)
            eng.status()
            eng.settle(1.0)
        except Dead as exc:
            cause = f"died: {exc}"
        except Hung as exc:
            cause = f"hang: {exc}"
        finally:
            if cause is None:
                eng.graceful_quit()
                if eng.alive():
                    cause = "hang: engine did not exit after quit"
            finding = classify_exit(eng, self.crashwatch, tag, 0, cause or "clean")
            (eng.work / "journal.txt").write_text("\n".join(lines) + "\n")
            eng.kill()
        # Every Finding is a reproduced oracle, including parser invariants,
        # filesystem escapes, assertions, and sanitizer output.  Restricting
        # this to crash/fatal/hang made regression replay silently pass known
        # non-crash bugs.
        return finding is not None, finding

    def replay_events(self, events, tag="replay", pace=None, source_root=None):
        """Replay the canonical ordered multi-actor event log."""
        pace = self.cfg.replay_pace if pace is None else pace
        source_root = source_root or self.replay_source
        actors = {}
        cause = None
        finding = None
        self.active_event_log = None

        try:
            for event in events:
                event_type = event.get("type")
                actor = event.get("actor", "client")
                if event_type == "actor_start":
                    if actor in actors:
                        continue
                    engine = self.make_engine(f"{tag}-{actor}", actor=actor)
                    if self.active_event_log is None:
                        self.active_event_log = EventLog(engine.work / "events.jsonl")
                        engine.event_log = self.active_event_log
                    engine.extra_args = list(event.get("extra_args", []))
                    if event.get("port") and event.get("rcon_password"):
                        engine.cfg.rcon_password = event["rcon_password"]
                        engine.set_local_endpoint(event["port"], event["rcon_password"])
                    engine.cfg.contain = bool(event.get("contain", getattr(engine.cfg, "contain", False)))
                    limits = event.get("limits", {})
                    for name, key, default in (
                            ("cpu_limit", "cpu", 300),
                            ("memory_limit_mb", "memory_mb", 4096),
                            ("file_limit_mb", "file_mb", 128),
                            ("fd_limit", "fd", 256),
                            ("process_limit", "processes", 0)):
                        setattr(engine.cfg, name, limits.get(key, getattr(engine.cfg, name, default)))
                    engine.start(boot_cmds=event.get("boot_cmds", []))
                    engine.wait_ready(timeout=self.cfg.boot_timeout)
                    actors[actor] = engine
                    continue

                engine = actors.get(actor)
                if engine is None:
                    raise Hung(f"event references actor before start: {actor}")

                if event_type == "policy":
                    engine.expected_errors = set(event.get("expected_errors", []))
                    continue

                if event_type == "input_install":
                    relative = event.get("destination", "")
                    if source_root and engine.restore_file(relative, source_root,
                                                           event.get("sha256")):
                        digest = hashlib.sha256(
                            (engine.work / "base" / relative).read_bytes()).hexdigest()
                        if event.get("sha256") and digest != event["sha256"]:
                            raise Hung(f"input hash mismatch for {relative}")
                        engine.fed_corrupt_input = True
                    else:
                        print(f"    warning: repro input missing: {relative}", flush=True)
                    continue

                if event_type != "actor_command":
                    continue
                commands = event.get("commands", [])
                if event.get("channel") == "rcon":
                    for command in commands:
                        engine.send_rcon(command)
                else:
                    engine.send(*commands)
                time.sleep(pace)

            for engine in actors.values():
                engine.status()
                engine.settle(1.0)
        except Dead as exc:
            cause = f"died: {exc}"
        except Hung as exc:
            cause = f"hang: {exc}"
        finally:
            for engine in reversed(list(actors.values())):
                if cause is None:
                    engine.graceful_quit()
                    if engine.alive():
                        cause = "hang: engine did not exit after quit"
            for actor, engine in actors.items():
                candidate = classify_exit(engine, self.crashwatch,
                                          f"{tag}/{actor}", 0, cause or "clean")
                if finding is None and candidate is not None:
                    finding = candidate
                (engine.work / "journal.txt").write_text(
                    "\n".join(engine.journal) + "\n")
                engine.kill()
            self.active_event_log = None

        return finding is not None, finding

    def regress(self, root):
        """Replay each saved repro under root and return failed reproductions."""
        root = Path(root).expanduser().resolve()
        if not root.is_dir():
            raise ValueError(f"regression directory not found: {root}")

        event_logs = sorted(root.rglob("repro.jsonl"))
        event_parents = {p.parent for p in event_logs}
        minimized = [p for p in sorted(root.rglob("repro.min.txt"))
                     if p.parent not in event_parents]
        selected_parents = event_parents | {p.parent for p in minimized}
        repros = (event_logs + minimized
                  + [p for p in sorted(root.rglob("repro.txt"))
                     if p.parent not in selected_parents])
        if not repros:
            raise ValueError(f"no repro.txt or repro.min.txt files under {root}")

        failures = []
        self.regression_total = len(repros)
        self.regression_results = []
        print(f"regression: {len(repros)} repros under {root}", flush=True)
        for source in repros:
            self.replay_source = source.parent
            lines = load_repro(source)
            rel = source.relative_to(root)
            tag = "regress-" + re.sub(r"[^A-Za-z0-9_.-]+", "-", str(rel))
            ok, finding = self.replay(lines, tag=tag)
            expected = None
            finding_file = source.parent / "FINDING.txt"
            if finding_file.exists():
                match = re.search(r"^signature: (.*)$", finding_file.read_text(), re.M)
                expected = match.group(1) if match else None
            same_oracle = ok and (expected is None
                                  or (finding is not None and finding.signature == expected))

            # A repro corpus is not a pass/fail suite: PASS here means "the bug
            # still reproduces".  known.json is what makes that readable -- an
            # open issue that still reproduces is expected, and one that has
            # stopped reproducing is a candidate for closing, not a failure.
            entry = self.known.match_signature(expected) or self.known.match(finding)
            state = self.known.state(entry) if entry is not None else None
            label = self.known.label(entry) if entry is not None else ""

            if state == "fixed" and ok:
                failures.append(source)
                result, detail = False, f"REGRESSION: known-fixed {label} reproduced"
            elif state == "fixed":
                result, detail = True, f"known-fixed {label}: stays fixed"
            elif state == "open" and same_oracle:
                result, detail = True, f"known-open {label}: still reproduces"
            elif state == "open":
                self.stale_known.append((str(rel), label))
                result, detail = True, (f"known-open {label}: no longer reproduces"
                                        " -- close it in known.json")
            elif same_oracle:
                result, detail = True, (finding.signature if finding else "finding")
            else:
                failures.append(source)
                result = False
                detail = ("no finding reproduced" if not ok else
                          f"oracle changed: {finding.signature if finding else 'none'}")

            self.regression_results.append((str(rel), result, detail))
            print(f"  {'PASS' if result else 'FAIL'} {rel} ({detail})", flush=True)
        return failures

    def _reproduces(self, lines, sig, tag, confirm=1):
        """Replay and require the same signature. confirm>1 re-runs for flaky bugs."""
        for k in range(confirm):
            ok, f = self.replay(lines, tag=f"{tag}.{k}" if confirm > 1 else tag)
            if not (ok and f is not None and f.signature == sig):
                return False
        return True

    def minimize(self, lines, budget=40, confirm=1):
        """Shrink a failing journal, structure first, then bytes.

        Plain line-chunk deletion fights dependencies -- deleting the connect
        but keeping the payload proves nothing.  So reduce in the order the
        journal is actually built: whole phases, then actions, then the binary
        payloads themselves.
        """
        ok, base = self.replay(lines, tag="min-verify")
        if not ok:
            print("    minimize: original journal does not reproduce; keeping as-is", flush=True)
            return lines, None
        sig = base.signature
        print(f"    minimize: reproduced ({sig}); shrinking {len(lines)} lines", flush=True)

        # "// file:" lines rebuild the fuzzed inputs and "@barrier" lines are how
        # the replay stays deterministic -- both are part of the repro, not noise
        def pinned_line(ln):
            t = ln.strip()
            return t.startswith("// file: ") or t.startswith("@barrier ")

        pinned = [ln for ln in lines if pinned_line(ln)]
        best = [ln for ln in lines if not pinned_line(ln)]
        attempts = [0]

        def try_candidate(cand, tag):
            if attempts[0] >= budget:
                return False
            attempts[0] += 1
            return self._reproduces(pinned + cand, sig, tag, confirm)

        # pass 1 -- chunked deletion, coarse to fine
        chunk = max(1, len(best) // 2)
        while chunk >= 1 and attempts[0] < budget:
            i, shrunk = 0, False
            while i < len(best) and attempts[0] < budget:
                cand = best[:i] + best[i + chunk:]
                if not cand:
                    break
                if try_candidate(cand, f"min{attempts[0]:02d}"):
                    best, shrunk = cand, True
                    print(f"      lines -> {len(best)}", flush=True)
                else:
                    i += chunk
            if not shrunk:
                chunk //= 2

        # pass 2 -- shrink injected wire payloads: whole records, then a binary
        # search on the tail, then individual bytes
        for idx, ln in enumerate(best):
            m = re.match(r"^(_stress_inject (?:reliable|datagram) )([0-9a-fA-F]+)$", ln.strip())
            if not m or attempts[0] >= budget:
                continue
            prefix, hexs = m.group(1), m.group(2)
            payload = bytes.fromhex(hexs)
            cut = len(payload)
            step = max(1, cut // 2)
            while step >= 1 and attempts[0] < budget:
                while cut - step >= 1 and attempts[0] < budget:
                    cand = list(best)
                    cand[idx] = prefix + payload[:cut - step].hex()
                    if try_candidate(cand, f"minb{attempts[0]:02d}"):
                        cut -= step
                        best = cand
                        print(f"      payload -> {cut} bytes", flush=True)
                    else:
                        break
                step //= 2
        return pinned + best, sig

# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------
def write_report(runner, path):
    cfg = runner.cfg
    lines = [
        "# QSS-M stress run",
        "",
        f"binary:    {cfg.binary}",
        f"level:    {getattr(cfg, 'stress_level', 'custom')}",
        f"scenarios: {', '.join(cfg.scenarios)}",
        f"iterations: {cfg.iterations}",
        f"seed:      {cfg.seed}",
        f"runs:      {runner.runs_done}",
        f"findings:  {len(runner.findings)} unique new"
        + (f", {len(runner.known_findings)} known" if runner.known_findings else ""),
        f"coverage:  {len(runner.coverage_ids)} ids ({runner.coverage_new} new)",
        f"corpus:    {runner.corpus_new} new, {runner.corpus_quarantined} redundant "
        f"inputs at {runner.corpus}",
        "",
    ]
    if not runner.findings:
        lines.append("No new crashes, hangs or fatal errors reproduced.")
    for f in runner.findings:
        lines += [
            f"## [{f.kind}] {f.signature}",
            f"scenario: {f.scenario}   seed: {f.seed}   "
            f"hits: {runner.seen_signatures.get(f.signature, 1)}",
            "",
            "```",
            f.detail.strip(),
            "```",
            "",
        ]
    if runner.known_findings:
        # Separate section on purpose: these are triaged, and burying the new
        # findings among them is how a real one gets skimmed past.
        lines += ["## Known issues seen again", "",
                  f"Suppressed by `{runner.known.path.name}`; not counted above.", "",
                  "| id | kind | signature | hits |", "| --- | --- | --- | --- |"]
        for f in runner.known_findings:
            entry = runner.known.match(f)
            lines.append(f"| {runner.known.label(entry)} | {f.kind} | "
                         f"`{f.signature}` | {runner.seen_signatures.get(f.signature, 1)} |")
        lines.append("")
    Path(path).write_text("\n".join(lines) + "\n")
    return path


def merge_parallel_corpus(workers, target):
    """Merge worker-local novelty inputs without making workers share state."""
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    copied = 0
    for worker in workers:
        source = Path(worker) / "corpus"
        if not source.is_dir():
            continue
        for path in source.rglob("*"):
            if not path.is_file() or path.suffix not in {".bin", ".json"}:
                continue
            if "quarantine" in path.relative_to(source).parts:
                continue
            destination = target / path.relative_to(source)
            if destination.exists():
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            try:
                shutil.copy2(path, destination)
                copied += 1
            except OSError:
                pass
    return copied


def parallel_campaign(raw_args, args):
    """Run isolated campaign workers and combine their durable artifacts."""
    if args.jobs < 1:
        raise ValueError("--jobs must be at least 1")
    if args.jobs == 1:
        return None
    if any((args.replay, args.minimize, args.regress, args.corpus_regress)):
        raise ValueError("--jobs is only supported for normal campaigns")
    if args.remote:
        raise ValueError("--jobs cannot be combined with --remote")

    results = Path(args.results).expanduser().resolve()
    results.mkdir(parents=True, exist_ok=True)
    lock = CampaignLock(results / ".campaign.lock")
    lock.acquire()
    workers = []
    processes = []
    logs = []
    try:
        worker_count = min(args.jobs, args.runs) if args.runs else args.jobs
        session = results / f"parallel-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
        session.mkdir(parents=True, exist_ok=False)
        base_port = (args.port_base if args.fixed_local else
                     args.port_base + secrets.randbelow(2048))
        base_seed = args.seed if args.seed is not None else secrets.randbelow(1 << 30)

        run_counts = [0] * worker_count
        if args.runs:
            quotient, remainder = divmod(args.runs, worker_count)
            run_counts = [quotient + (index < remainder)
                          for index in range(worker_count)]

        print(f"parallel campaign: {worker_count} workers, results under {session}",
              flush=True)
        for index in range(worker_count):
            worker = session / f"worker-{index + 1:02d}"
            worker.mkdir()
            workers.append(worker)
            child_args = list(raw_args)
            child_args += [
                "--jobs", "1",
                "--results", str(worker),
                "--corpus", str(worker / "corpus"),
                "--seed", str((base_seed + index * 0x9E3779B9) & 0x3FFFFFFF),
                # Leave ample room for helper engines and port retries.  The
                # old 128-port spacing was safe only for short campaigns.
                "--port-base", str(base_port + index * 4096),
                "--fixed-local",
            ]
            if args.runs:
                child_args += ["--runs", str(run_counts[index])]
            elif args.minutes:
                child_args += ["--minutes", str(args.minutes)]

            log_path = worker / "worker.log"
            log = log_path.open("wb")
            logs.append(log)
            process = subprocess.Popen(
                [sys.executable, str(Path(__file__).resolve()), *child_args],
                cwd=REPO, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True)
            processes.append(process)
            print(f"  worker {index + 1}: pid={process.pid} log={log_path}", flush=True)

        returncodes = []
        for index, process in enumerate(processes):
            returncodes.append(process.wait())
            print(f"  worker {index + 1}: exit={returncodes[-1]}", flush=True)

        corpus_target = (Path(args.corpus).expanduser().resolve()
                         if args.corpus else results / "corpus")
        merged = merge_parallel_corpus(workers, corpus_target)
        report = results / "PARALLEL_REPORT.md"
        report_lines = [
            "# QSS-M parallel stress run", "",
            f"workers: {worker_count}",
            f"seed: {base_seed}",
            f"corpus: {corpus_target} ({merged} files merged)", "",
            "| worker | exit | artifacts |", "| --- | ---: | --- |",
        ]
        report_lines += [
            f"| {index + 1} | {code} | `{worker}` |"
            for index, (worker, code) in enumerate(zip(workers, returncodes))
        ]
        report.write_text("\n".join(report_lines) + "\n")
        print(f"parallel report: {report}", flush=True)
        return 1 if any(code != 0 for code in returncodes) else 0
    except KeyboardInterrupt:
        print("\nparallel campaign interrupted", flush=True)
        for process in processes:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except (ProcessLookupError, PermissionError):
                    process.terminate()
        return 130
    finally:
        for log in logs:
            log.close()
        lock.release()


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="QSS-M stress tester / crash finder")
    ap.add_argument("--bin", help="path to the QSS-M executable")
    ap.add_argument("--paks", help="directory holding pak0.pak/pak1.pak")
    ap.add_argument("--loose-root", help="game root containing loose gfx.wad/assets")
    ap.add_argument("--results", default=str(Path(__file__).parent / "tmp" / "results"))
    ap.add_argument("--corpus", help="persistent novelty corpus directory (default: results/corpus)")
    ap.add_argument("--scenario", action="append", default=[],
                    help="scenario to run (repeatable); default: all but smoke")
    ap.add_argument("--stress-level", choices=sorted(STRESS_LEVELS), default="standard",
                    help="named campaign schedule: smoke, standard, deep, or soak")
    ap.add_argument("--runs", type=int, default=0, help="number of runs (0 = unlimited)")
    ap.add_argument("--minutes", type=float, default=0, help="wall-clock budget")
    ap.add_argument("--jobs", type=int, default=1,
                    help="parallel isolated campaign workers (default: 1)")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--iterations", type=int, default=None,
                    help="override the selected stress level's per-scenario depth")
    ap.add_argument("--round-robin", action="store_true",
                    help="cycle scenarios in order instead of picking randomly")
    ap.add_argument("--allow-net", action="store_true",
                    help="include menus/commands that talk to the internet")
    ap.add_argument("--remote", metavar="HOST:PORT",
                    help="benign connect-and-play coverage target for the 'remote' "
                         "scenario, e.g. denver.quakeone.com:26000 (never fuzzed)")
    ap.add_argument("--sound", action="store_true", help="do not pass -nosound")
    ap.add_argument("--nosound", action="store_true",
                    help="force -nosound even for levels that exercise audio")
    ap.add_argument("--contain", action="store_true",
                    help="macOS Seatbelt containment: run-dir writes and loopback only")
    ap.add_argument("--cpu-limit", type=int, default=300,
                    help="per-engine CPU limit in seconds")
    ap.add_argument("--memory-limit-mb", type=int, default=4096,
                    help="per-engine address-space limit (0 disables)")
    ap.add_argument("--file-limit-mb", type=int, default=128,
                    help="per-file write limit")
    ap.add_argument("--fd-limit", type=int, default=256,
                    help="per-engine open-file limit")
    ap.add_argument("--process-limit", type=int, default=0,
                    help="optional per-user process limit (0 disables)")
    ap.add_argument("--boot-timeout", type=float, default=90)
    ap.add_argument("--map-timeout", type=float, default=90)
    ap.add_argument("--hang-timeout", type=float, default=25)
    ap.add_argument("--hang-retries", type=int, default=2,
                    help="extra liveness probes before declaring a hang")
    ap.add_argument("--port-base", type=int, default=26200)
    ap.add_argument("--rcon-password",
                    help="override the random localhost RCON password")
    ap.add_argument("--fixed-local", action="store_true",
                    help="do not randomize the localhost port base")
    ap.add_argument("--replay", help="replay a repro.txt journal")
    ap.add_argument("--minimize", help="minimize a repro.txt journal")
    ap.add_argument("--regress", metavar="DIR",
                    help="replay saved repros below DIR; prefer repro.min.txt when present")
    ap.add_argument("--corpus-regress", action="store_true",
                    help="replay every saved novelty input through its original lane")
    ap.add_argument("--confirm", type=int, default=1,
                    help="require N matching reproductions per shrink (flaky signatures)")
    ap.add_argument("--known", default=str(KNOWN_PATH),
                    help="triaged-signature list (default: known.json beside this script)")
    ap.add_argument("--no-known", action="store_true",
                    help="report known issues as new findings")
    ap.add_argument("--known-add", metavar="PATH",
                    help="add the signature of a FINDING.txt (or a result directory) "
                         "to known.json and exit")
    ap.add_argument("--known-state", choices=("open", "fixed"), default="open",
                    help="state for --known-add (default: open)")
    ap.add_argument("--known-note", default="",
                    help="note stored with --known-add")
    ap.add_argument("--known-list", action="store_true",
                    help="print the known-issue list and exit")
    ap.add_argument("--list", action="store_true", help="list scenarios and exit")
    ap.add_argument("--keep", action="store_true", help="keep artifacts for clean runs")
    args = ap.parse_args(argv)

    if args.list:
        for name, fn in SCENARIOS.items():
            status = LANES[name].get("status", "implemented")
            note = "" if status == "implemented" else f"  [{status}]"
            print(f"{name:15s} {(fn.__doc__ or '').strip().splitlines()[0]}{note}")
        for name in PLANNED_LANES:
            print(f"{name:15s} (planned -- declared in lanes.json, not implemented)")
        return 0

    known = KnownIssues(args.known, enabled=not args.no_known)

    if args.known_list:
        if not known.issues:
            print(f"no known issues in {known.path}")
        for entry in known.issues:
            print(f"{entry.get('id', '?'):10s} {known.state(entry):6s} "
                  f"{entry.get('signature') or entry.get('signature_re')}")
            if entry.get("note"):
                print(f"{'':10s}        {entry['note']}")
        return 0

    if args.known_add:
        source = Path(args.known_add).expanduser()
        if source.is_dir():
            source = source / "FINDING.txt"
        if not source.is_file():
            sys.exit(f"no FINDING.txt at {source}")
        text = source.read_text()
        fields = {key: (re.search(rf"^{key}: (.*)$", text, re.M) or [None, ""])[1]
                  for key in ("kind", "scenario", "signature")}
        if not fields["signature"]:
            sys.exit(f"no signature line in {source}")
        entry = known.add(Finding(fields["kind"] or "", fields["scenario"] or "", 0,
                                  fields["signature"], ""),
                          state=args.known_state, note=args.known_note)
        if entry is None:
            print(f"already known: {fields['signature']}")
        else:
            print(f"added {entry['id']} ({entry['state']}) to {known.path}\n"
                  f"  {entry['signature']}")
        return 0

    if args.contain:
        if sys.platform != "darwin" or shutil.which("sandbox-exec") is None:
            sys.exit("--contain requires macOS sandbox-exec")
        if args.remote:
            sys.exit("--contain is loopback-only and cannot be combined with --remote")
        if args.allow_net:
            sys.exit("--contain is loopback-only and cannot be combined with --allow-net")

    if args.jobs < 1:
        ap.error("--jobs must be at least 1")
    if args.jobs > 1:
        try:
            raw_args = list(argv) if argv is not None else sys.argv[1:]
            return parallel_campaign(raw_args, args)
        except ValueError as exc:
            ap.error(str(exc))

    paks = find_paks(args.paks)
    loose_root = find_loose_root(args.loose_root, paks)
    level = STRESS_LEVELS[args.stress_level]
    if args.sound and args.nosound:
        ap.error("--sound and --nosound are mutually exclusive")
    scenarios = args.scenario or list(level["scenarios"])
    for s in scenarios:
        if s not in SCENARIOS:
            sys.exit(f"unknown scenario: {s} (see --list)")

    cfg = Config(
        binary=find_binary(args.bin),
        paks=paks,
        loose_root=loose_root,
        registered=any(p.name == "pak1.pak" for p in paks),
        results=args.results,
        corpus=args.corpus or str(Path(args.results).expanduser() / "corpus"),
        scenarios=scenarios,
        runs=args.runs,
        minutes=args.minutes,
        seed=args.seed if args.seed is not None else random.randrange(1 << 30),
        iterations=args.iterations if args.iterations is not None else level["iterations"],
        round_robin=args.round_robin or level["round_robin"],
        stress_level=args.stress_level,
        level_passes=level["passes"],
        allow_net=args.allow_net,
        remote=args.remote,
        nosound=args.nosound or not (args.sound or level.get("sound", False)),
        contain=args.contain,
        cpu_limit=args.cpu_limit,
        memory_limit_mb=args.memory_limit_mb,
        file_limit_mb=args.file_limit_mb,
        fd_limit=args.fd_limit,
        process_limit=args.process_limit,
        boot_timeout=args.boot_timeout,
        map_timeout=args.map_timeout,
        hang_timeout=args.hang_timeout,
        hang_retries=args.hang_retries,
        port_base=args.port_base if args.fixed_local else args.port_base + secrets.randbelow(2048),
        rcon_password=args.rcon_password or secrets.token_hex(16),
        replay_pace=0.05,
        always_check=True,
        keep=args.keep,
        known=known,
    )
    modes = sum(bool(x) for x in (args.replay, args.minimize, args.regress,
                                  args.corpus_regress))
    if modes > 1:
        ap.error("--replay, --minimize, and --regress are mutually exclusive")
    if not cfg.runs and not cfg.minutes and not modes:
        if level["minutes"]:
            cfg.minutes = level["minutes"]
        else:
            cfg.runs = len(scenarios) * level["passes"]

    print(f"binary:     {cfg.binary}")
    print(f"paks:       {', '.join(p.name for p in cfg.paks)} "
          f"({'registered' if cfg.registered else 'shareware'})")
    print(f"scenarios:  {', '.join(cfg.scenarios)}")
    print(f"seed:       {cfg.seed}")
    print()

    runner = Runner(cfg)

    if args.corpus_regress:
        failures = runner.corpus_regress()
        report = runner.results / "CORPUS_REGRESSION.md"
        report_lines = [
            "# QSS-M corpus regression", "",
            f"corpus: {runner.corpus}",
            f"inputs: {runner.regression_total}",
            f"failures: {len(failures)}", "", "| input | result | detail |",
            "| --- | --- | --- |",
        ]
        report_lines += [f"| `{name}` | {'PASS' if ok else 'FAIL'} | {detail} |"
                         for name, ok, detail in runner.regression_results]
        report.write_text("\n".join(report_lines) + "\n")
        print(f"corpus regression report: {report}")
        return 1 if failures else 0

    if args.regress:
        try:
            failures = runner.regress(args.regress)
        except ValueError as exc:
            sys.exit(str(exc))
        report = runner.results / "REGRESSION.md"
        report_lines = [
            "# QSS-M stress regression", "",
            f"source: {Path(args.regress).expanduser().resolve()}",
            f"repros: {runner.regression_total}",
            f"failures: {len(failures)}",
            f"known list: {runner.known.path if runner.known.enabled else 'disabled'}",
            "", "| repro | result | detail |",
            "| --- | --- | --- |",
        ]
        report_lines += [f"| `{name}` | {'PASS' if ok else 'FAIL'} | {detail} |"
                         for name, ok, detail in runner.regression_results]
        if runner.stale_known:
            report_lines += ["", "## Known-open issues that no longer reproduce", "",
                             "Verify the fix, then set `state: fixed` in "
                             f"`{runner.known.path.name}` so a return is caught.", ""]
            report_lines += [f"- {label or '(unlabelled)'} -- `{name}`"
                             for name, label in runner.stale_known]
        report.write_text("\n".join(report_lines) + "\n")
        print(f"regression report: {report}")
        for name, label in runner.stale_known:
            print(f"  stale known-open: {label} ({name}) -- may be fixed", flush=True)
        return 1 if failures else 0

    if args.replay or args.minimize:
        src = Path(args.replay or args.minimize)
        try:
            lines = load_repro(src)
        except ValueError as exc:
            sys.exit(str(exc))
        if args.minimize and lines and isinstance(lines[0], dict):
            sys.exit("--minimize accepts legacy text journals, not repro.jsonl")
        runner.replay_source = src.parent
        if args.replay:
            ok, finding = runner.replay(lines)
            print(f"reproduced: {ok}" + (f"  ({finding.signature})" if finding else ""))
            return 0 if not ok else 1
        best, sig = runner.minimize(lines, confirm=args.confirm)
        out = src.with_name(src.stem + ".min.txt")
        out.write_text("\n".join(best) + "\n")
        print(f"minimized to {len(best)} lines -> {out}")
        return 0

    try:
        runner.campaign()
    except KeyboardInterrupt:
        print("\ninterrupted", flush=True)

    runner.compact_corpus()

    report = write_report(runner, runner.results / "REPORT.md")
    print()
    known_note = (f", {len(runner.known_findings)} known"
                  if runner.known_findings else "")
    print(f"{runner.runs_done} runs, {len(runner.findings)} unique new findings{known_note}")
    for f in runner.findings:
        print(f"  [{f.kind}] {f.signature}")
    for f in runner.known_findings:
        print(f"  [known] {f.kind}: {f.signature}")
    print(f"report: {report}")
    return 1 if runner.findings else 0


if __name__ == "__main__":
    sys.exit(main())
