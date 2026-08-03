#!/usr/bin/env python3
"""
RCON state probe for QSS-M crash hunting.

This is the focused companion to qssm_stress.py.  It reproduces the external
RCON workflow used to find state-dependent crashes such as viewpos, while
reusing the main harness' sandbox, engine lifecycle, crash-report parsing,
status probes, journals, and finding classification.

It never talks to a third-party server.  Every run starts a local listen
server in its own throwaway -basedir and sends RCON packets only to localhost.

Examples:
    ./rcon_probe.py --list
    ./rcon_probe.py --map start --runs 3
    ./rcon_probe.py --map start --iterations 20 --seed 1234
    ./rcon_probe.py --bin /path/to/QSS-M --paks ~/Desktop/qssm/id1/paks
"""

import argparse
import random
import secrets
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qssm_stress as S


POSITIONS = [
    (0, 0, 0, 0, 0),
    (128, 336, 416, 0, 90),
    (-2537, -656, -192, 1, 340),
    (4096, -4096, 2048, -89, 180),
    (-8000, 8000, -8000, 89, 359),
]

STATE_COMMANDS = [
    "viewpos",
    "entities",
    "edictcount",
    "edicts",
    "setpos last",
    "status",
]

RENDER_COMMANDS = [
    "r_drawentities 0",
    "r_drawentities 1",
    "r_fullbright 0",
    "r_fullbright 1",
    "r_wateralpha 0",
    "r_wateralpha 1",
    "scr_diag 0",
]

DEMO_COMMANDS = [
    "viewpos",
    "entities",
    "edictcount",
    "jumpdemo -1",
    "jumpdemo 0",
    "jumpdemo 1",
    "jumpdemo 99999",
    "pause",
    "pause",
]


def make_config(args, results):
    paks = S.find_paks(args.paks)
    return S.Config(
        binary=S.find_binary(args.binary),
        paks=paks,
        loose_root=S.find_loose_root(args.loose_root, paks),
        registered=any(p.name == "pak1.pak" for p in paks),
        results=str(results),
        scenarios=["rcon-probe"],
        runs=1,
        minutes=0,
        seed=args.seed,
        iterations=args.iterations,
        round_robin=False,
        allow_net=False,
        remote=None,
        nosound=not args.sound,
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
    )


def run_rcon(eng, command, reply_log):
    """Send one localhost RCON command and retain its console reply."""
    eng.journal.append("// rcon: " + command)
    if eng.event_log:
        eng.event_log.record("actor_command", actor=eng.actor,
                             channel="rcon", commands=[command])
    reply = eng.rcon.send(command, want_reply=True)
    with reply_log.open("a", encoding="utf-8", errors="replace") as fh:
        fh.write(f">>> {command}\n")
        fh.write((reply or "<no reply>") + "\n")
    if reply is None and not eng.alive():
        raise S.Dead(f"engine exited during RCON command: {command}")
    shown = " ".join((reply or "<no reply>").split())
    print(f"    rcon {command!r} -> {shown[:180]}", flush=True)
    return reply or ""


def probe_commands(eng, commands, reply_log, label, cfg, settle=0.08):
    """Run commands in one state, checking process liveness after each one."""
    print(f"  [{label}] {len(commands)} commands", flush=True)
    for command in commands:
        run_rcon(eng, command, reply_log)
        if settle:
            time.sleep(settle)
        # The stress channel gives us a deterministic liveness check even when
        # the RCON command itself prints no output.
        eng.status(timeout=min(cfg.hang_timeout, 10.0))


def reopen_listen_socket(eng, cfg):
    """Re-open the local RCON listener after a map teardown/rebuild."""
    eng.send("listen 1")
    eng.status(timeout=min(cfg.hang_timeout, 10.0))


def probe_spawned_state(eng, rng, cfg, reply_log, label):
    """Probe several camera positions and render/cvar combinations in-level."""
    for i in range(cfg.iterations):
        if i < len(POSITIONS):
            x, y, z, pitch, yaw = POSITIONS[i]
        else:
            x = rng.randint(-9000, 9000)
            y = rng.randint(-9000, 9000)
            z = rng.randint(-4000, 4000)
            pitch = rng.randint(-90, 90)
            yaw = rng.randint(0, 359)

        run_rcon(eng, f"setpos {x} {y} {z} {pitch} {yaw} 0", reply_log)
        probe_commands(eng, STATE_COMMANDS, reply_log, f"{label}/pos-{i}", cfg)
        if i % 2 == 0:
            probe_commands(eng, RENDER_COMMANDS, reply_log,
                           f"{label}/render-{i}", cfg)


def run_probe(runner, cfg, args, run_number, seed):
    rng = random.Random(seed)
    tag = f"rcon-probe-{run_number}"
    eng = runner.make_engine(tag)
    runner.active_event_log = S.EventLog(eng.work / "events.jsonl")
    eng.event_log = runner.active_event_log
    reply_log = eng.work / "rcon-replies.log"
    cause = None
    finding = None

    try:
        print(f"[run {runner.run_index:04d}] {tag} seed={seed} "
              f"map={args.map_name} port={eng.port}", flush=True)

        # The autoexec installs rcon_password.  Enable the listen server
        # through the stress channel after boot; this is more reliable than a
        # +listen command-line argument and leaves the RCON UDP endpoint ready
        # before the first external probe.
        eng.start()
        eng.wait_ready(timeout=cfg.boot_timeout)
        eng.stress_event("rcon-probe", phase="L0", loader_hit=None,
                         parser="rcon", protocol="local")
        eng.send("listen 1", f"map {args.map_name}")
        eng.wait_spawned(timeout=cfg.map_timeout)

        # The first external pass is fully spawned.  The client/server remains
        # on a live local listen server, so RCON replies are available.
        probe_commands(eng, STATE_COMMANDS, reply_log,
                       "initial-spawned", cfg)

        # Start another map through the internal channel and immediately send
        # RCON probes while signon is in flight.
        eng.send(f"map {args.map_name}")
        probe_commands(eng, STATE_COMMANDS[:4], reply_log,
                       "early-signon", cfg, settle=0.0)
        eng.wait_spawned(timeout=cfg.map_timeout)
        reopen_listen_socket(eng, cfg)

        # The main target: state-sensitive client commands at known and random
        # positions, followed by the render/cvar combinations from the manual
        # RCON workflow.
        probe_spawned_state(eng, rng, cfg, reply_log, "spawned")

        # Save/load and death are common transitions where client/server state
        # can be temporarily incomplete.
        save_name = f"rconprobe{run_number}"
        probe_commands(eng, [f"save {save_name}", f"load {save_name}"],
                       reply_log, "save-load", cfg, settle=0.25)
        reopen_listen_socket(eng, cfg)
        run_rcon(eng, "kill", reply_log)
        probe_commands(eng, STATE_COMMANDS, reply_log, "dead", cfg)

        # Menus can leave a command looking at a different client state while
        # the server remains alive.
        probe_commands(eng, ["togglemenu 1", "menu_options"] + STATE_COMMANDS,
                       reply_log, "menu", cfg)
        run_rcon(eng, "togglemenu 1", reply_log)

        # Changelevel tests the same probes across a teardown/rebuild boundary.
        second = args.second_map or ("dm1" if args.map_name != "dm1" else "start")
        run_rcon(eng, f"changelevel {second}", reply_log)
        probe_commands(eng, STATE_COMMANDS[:4], reply_log,
                       "changelevel", cfg, settle=0.0)
        eng.wait_spawned(timeout=cfg.map_timeout)
        reopen_listen_socket(eng, cfg)
        probe_spawned_state(eng, rng, cfg, reply_log, "post-changelevel")

        # Record a short local demo, then exercise seeking and client queries
        # while demo playback owns the normal network state.
        demo_name = f"rconprobe_demo{run_number}"
        probe_commands(eng, [f"record {demo_name}", "+forward", "-forward",
                             "stop", "disconnect", f"playdemo {demo_name}"],
                       reply_log, "demo-start", cfg, settle=0.2)
        probe_commands(eng, DEMO_COMMANDS, reply_log, "demo-playback", cfg)
        run_rcon(eng, "disconnect", reply_log)
        eng.status(timeout=min(cfg.hang_timeout, 10.0))

    except S.Dead as exc:
        cause = f"died: {exc}"
    except S.Hung as exc:
        cause = f"hang: {exc}"
        sample = eng.sample()
        if sample:
            cause += f"\nstack sample: {sample}"
    except Exception as exc:
        cause = f"harness error: {exc!r}"
    finally:
        if cause is None:
            eng.graceful_quit()
            if eng.alive():
                cause = "hang: engine did not exit after quit"
        (eng.work / "journal.txt").write_text("\n".join(eng.journal) + "\n")
        if cause is not None or cfg.always_check:
            finding = S.classify_exit(eng, runner.crashwatch, tag, seed,
                                      cause or "clean")
        eng.kill()
        eng.read_log()
        runner.active_event_log = None

    if finding:
        runner.record(finding, eng)
    elif cause:
        print(f"    (unclassified: {cause})", flush=True)
    else:
        print("    clean", flush=True)
    runner.runs_done += 1
    return finding


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", dest="binary", help="path to the QSS-M executable")
    ap.add_argument("--paks", help="directory holding pak0.pak/pak1.pak")
    ap.add_argument("--loose-root", help="game root containing loose gfx.wad/assets")
    ap.add_argument("--results", default=str(Path(__file__).parent / "tmp" / "rcon-probe"))
    ap.add_argument("--map", dest="map_name", default="start")
    ap.add_argument("--second-map", help="map used for the changelevel phase")
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--iterations", type=int, default=8,
                    help="camera positions per spawned-state phase")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--rcon-password",
                    help="override the random localhost RCON password")
    ap.add_argument("--port-base", type=int, default=28200)
    ap.add_argument("--fixed-local", action="store_true",
                    help="do not randomize the localhost port base")
    ap.add_argument("--boot-timeout", type=float, default=90)
    ap.add_argument("--map-timeout", type=float, default=90)
    ap.add_argument("--hang-timeout", type=float, default=20)
    ap.add_argument("--hang-retries", type=int, default=2)
    ap.add_argument("--sound", action="store_true", help="do not pass -nosound")
    ap.add_argument("--contain", action="store_true",
                    help="macOS Seatbelt containment: run-dir writes and loopback only")
    ap.add_argument("--cpu-limit", type=int, default=300)
    ap.add_argument("--memory-limit-mb", type=int, default=4096)
    ap.add_argument("--file-limit-mb", type=int, default=128)
    ap.add_argument("--fd-limit", type=int, default=256)
    ap.add_argument("--process-limit", type=int, default=0)
    ap.add_argument("--keep", action="store_true", help="keep clean artifacts")
    ap.add_argument("--list", action="store_true", help="list probe phases")
    args = ap.parse_args(argv)

    if args.list:
        for name in ("initial-spawned", "early-signon", "spawned",
                     "save-load", "dead", "menu", "changelevel",
                     "demo-playback"):
            print(name)
        return 0

    if args.contain and (sys.platform != "darwin" or S.shutil.which("sandbox-exec") is None):
        sys.exit("--contain requires macOS sandbox-exec")

    results = Path(args.results).expanduser().resolve()
    cfg = make_config(args, results)
    runner = S.Runner(cfg)
    seeds = random.Random(args.seed).randrange(1 << 30) if args.seed is not None else None
    findings = []

    for i in range(args.runs):
        seed = (seeds + i) if seeds is not None else random.randrange(1 << 30)
        findings.append(run_probe(runner, cfg, args, i + 1, seed))

    report = S.write_report(runner, results / "REPORT.md")
    print(f"\n{args.runs} probe run(s), {len(runner.findings)} unique findings")
    print(f"report: {report}")
    return 1 if runner.findings else 0


if __name__ == "__main__":
    sys.exit(main())
