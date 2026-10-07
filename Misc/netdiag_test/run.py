#!/usr/bin/env python3
"""
run.py -- netgraph end-to-end scenarios  // woods #netgraph

Launches a listen server, the fault proxy, and a client that records with
`netgraph record`, then compares the client's JSON report with the proxy's
ground-truth log. Prints a JSON summary; exit 0 = every scenario passed.

  Misc/netdiag_test/run.py [scenario ...] [--bin PATH] [--keep]

Harness rules learned the hard way (see project notes):
- resolve the binary from xcodebuild's BUILT_PRODUCTS_DIR, never macOS/build/Debug
- run in a throwaway -basedir: the engine rewrites config.cfg
- a pure client can't be rcon'd, and wait-loops block signon, so the client is
  driven only by its command line and scheduled _netdiag_* dev commands
- check for stray QSS-M processes with pgrep -x, not -f
"""

import argparse
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PAKS = os.path.expanduser("~/Desktop/qssm/id1/paks")

SERVER_PORT = 26100
PROXY_PORT = 26200
CLIENT_PORT = 26300
RCON_PW = "ndtest"

# gameplay starts a few seconds after connect; faults are timed from the first
# server->client unreliable datagram, so leave room for signon
SCENARIOS = {
    "clean":     {"seconds": 14, "faults": []},
    "drop":      {"seconds": 20, "faults": [{"type": "drop", "dir": "s2c", "at": 8, "count": 1},
                                             {"type": "drop", "dir": "s2c", "at": 13, "count": 3}]},
    "delay":     {"seconds": 20, "faults": [{"type": "delay", "dir": "s2c", "at": 9, "ms": 120}]},
    "stall":     {"seconds": 20, "faults": [], "stall": (150, 6)},
    "dup":       {"seconds": 16, "faults": [{"type": "dup", "dir": "s2c", "at": 8}]},
    "reorder":   {"seconds": 16, "faults": [{"type": "reorder", "dir": "s2c", "at": 8}]},
    "blackout":  {"seconds": 22, "faults": [{"type": "blackout", "dir": "s2c", "at": 9, "ms": 1500}]},
    # scr_ping's own 5 s "ping" request is the client's reliable traffic here
    "resend":    {"seconds": 20, "faults": [{"type": "drop_rel", "dir": "c2s", "at": 8}], "scr_ping": 1},
    "pause":     {"seconds": 20, "faults": [], "rcon_at": [(8, "pause"), (11, "pause")]},
    "mapchange": {"seconds": 26, "faults": [], "rcon_at": [(8, "changelevel e1m2")]},
    # LATE validation at a low frame cap: reads happen once per 33 ms frame
    "clean30":   {"seconds": 14, "faults": [], "maxfps": 30},
    "delay30":   {"seconds": 20, "faults": [{"type": "delay", "dir": "s2c", "at": 9, "ms": 120}], "maxfps": 30},
    # same session with recording off; compared against "clean" through the proxy log
    "passive_off": {"seconds": 14, "faults": [], "record": False},
    # review fixes: recording paused mid-game must not become a hitch; a stall 1 s before
    # disconnecting must still be saved
    "offon":     {"seconds": 16, "faults": [], "after": [(5, "netgraph off"), (7, "netgraph record")]},
    "endhitch":  {"seconds": 7, "faults": [], "stall": (150, 6)},
    # snapshots stop while chat keeps the connection alive: NO UPDATES must still show
    "chatsilence": {"seconds": 18, "faults": [{"type": "blackout_unrel", "dir": "s2c", "at": 7, "ms": 5000}],
                    "rcon_at": [(t / 2.0, "say chat") for t in range(14, 26)]},
    # a map restart starts the move sequence over: command round trip must keep updating
    "restart":   {"seconds": 20, "faults": [], "rcon_at": [(8, "restart")]},
}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def find_binary():
    out = subprocess.run(["xcodebuild", "-project", os.path.join(REPO, "macOS", "QuakeSpasm.xcodeproj"),
                          "-scheme", "QSS-M", "-configuration", "Debug", "-showBuildSettings"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("BUILT_PRODUCTS_DIR = "):
            return os.path.join(line.split(" = ", 1)[1], "QSS-M.app", "Contents", "MacOS", "QSS-M")
    raise SystemExit("couldn't resolve BUILT_PRODUCTS_DIR")


def make_sandbox(root, scr_ping=0, maxfps=250):
    id1 = os.path.join(root, "id1")
    os.makedirs(id1, exist_ok=True)
    for pak in ("pak0.pak", "pak1.pak"):
        dst = os.path.join(id1, pak)
        if not os.path.exists(dst):
            os.symlink(os.path.join(PAKS, pak), dst)
    with open(os.path.join(id1, "autoexec.cfg"), "w") as f:
        f.write("vid_vsync 0\nhost_maxfps %d\nscr_ping %d\n" % (maxfps, scr_ping))
    return root


def rcon(cmd, port=SERVER_PORT):
    body = bytes([0x05]) + RCON_PW.encode() + b"\0" + cmd.encode() + b"\0"
    pkt = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(1.0)
    s.sendto(pkt, ("127.0.0.1", port))
    try:
        data, _ = s.recvfrom(65535)
        return data[5:].decode(errors="replace").strip("\0\n ")
    except socket.timeout:
        return None
    finally:
        s.close()


def stray_instances():
    r = subprocess.run(["pgrep", "-x", "QSS-M"], capture_output=True, text=True)
    return [p for p in r.stdout.split() if p]


def wait_for(path, needle, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with open(path, errors="replace") as f:
                text = f.read()
            if needle in text:
                return text
        except FileNotFoundError:
            pass
        time.sleep(0.2)
    return None


def read_jsonl(path):
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def run_scenario(name, spec, binary, work, keep):
    d = os.path.join(work, name)
    os.makedirs(d, exist_ok=True)
    base = make_sandbox(os.path.join(d, "base"), spec.get("scr_ping", 0), spec.get("maxfps", 250))
    record = spec.get("record", True)
    common = ["-basedir", base, "-noquakeimport", "-window", "-width", "480", "-height", "300", "-nosound"]
    procs = []
    result = {"scenario": name, "pass": False, "checks": []}

    def check(ok, what):
        result["checks"].append({"ok": bool(ok), "what": what})
        return ok

    try:
        srv_log = open(os.path.join(d, "server.log"), "w")
        procs.append(subprocess.Popen([binary] + common + ["-port", str(SERVER_PORT), "-listen", "4",
                                       "+rcon_password", RCON_PW, "+map", "e1m1"],
                                      stdout=srv_log, stderr=subprocess.STDOUT, cwd=d))
        if not wait_for(os.path.join(d, "server.log"), "entered the game", 30):
            check(False, "server came up")
            return result
        time.sleep(3)

        faults = os.path.join(d, "faults.json")
        with open(faults, "w") as f:
            json.dump(spec["faults"], f)
        procs.append(subprocess.Popen([sys.executable, os.path.join(HERE, "udpproxy.py"), "--listen", str(PROXY_PORT),
                                       "--server", "127.0.0.1:%d" % SERVER_PORT, "--log", os.path.join(d, "proxy.jsonl"),
                                       "--faults", faults, "--duration", str(spec["seconds"] + 60)], cwd=d))
        time.sleep(0.5)

        if record:
            cl_args = ["+developer", "1", "+name", "ndclient", "+_netdiag_ignorefocus", "1", "+netgraph", "record",
                       "+_netdiag_autostatus", "1", "+_netdiag_autoreport", "1",
                       "+_netdiag_disconnect_after", str(spec["seconds"])]
        else:
            cl_args = ["+developer", "1", "+name", "ndclient"]
        for t, cmd in spec.get("after", []):
            cl_args += ["+_netdiag_after", str(t)] + cmd.split()
        if "stall" in spec:
            cl_args += ["+_netdiag_stall", str(spec["stall"][0]), str(spec["stall"][1])]
        cl_args += ["+connect", "127.0.0.1:%d" % PROXY_PORT]
        cl_log_path = os.path.join(d, "client.log")
        cl_log = open(cl_log_path, "w")
        procs.append(subprocess.Popen([binary] + common + ["-port", str(CLIENT_PORT)] + cl_args,
                                      stdout=cl_log, stderr=subprocess.STDOUT, cwd=d))

        if not wait_for(cl_log_path, "state=live" if record else "connect timing: in game", 40):
            check(False, "client reached live state")
            return result
        if not record:
            time.sleep(spec["seconds"])
            result["proxy"] = os.path.join(d, "proxy.jsonl")
            result["pass"] = True
            return result

        t_live = time.time()
        timed = sorted(spec.get("rcon_at", []))
        while time.time() - t_live < spec["seconds"]:
            if timed and time.time() - t_live >= timed[0][0]:
                rcon(timed.pop(0)[1])
            time.sleep(0.1)

        text = wait_for(cl_log_path, "netgraph: wrote", 20)
        if not check(text is not None, "client wrote its report on disconnect"):
            return result
        time.sleep(1.0)
        with open(cl_log_path, errors="replace") as f:
            text = f.read()
        paths = [ln.split("netgraph: wrote ", 1)[1].strip() for ln in text.splitlines() if "netgraph: wrote " in ln]
        jpath = next((p for p in paths if p.endswith(".json")), None)
        if not check(jpath and os.path.exists(jpath), "json report exists"):
            return result
        with open(jpath) as f:
            rep = json.load(f)
        shutil.copy(jpath, os.path.join(d, "report.json"))
        result["report"] = {k: rep["session"].get(k) for k in ("rec_s", "frames", "missing", "gaps", "dup", "stale",
                            "resent", "late_median_ms", "late_p99_ms", "jitter_ms", "frame_median_ms", "frame_p99_ms",
                            "starved_ms", "in_unreliable_pkts", "in_unreliable_bytes", "in_ack_pkts", "in_reliable_pkts",
                            "out_unreliable_pkts", "out_reliable_pkts", "out_ack_pkts")}
        result["hitches"] = [(h["kind"], round(h["frame_ms"]), round(h["late_ms"]), h["missing"], h["sentence"])
                             for h in rep["hitches"]]

        result["proxy"] = os.path.join(d, "proxy.jsonl")
        proxy = [r for r in read_jsonl(os.path.join(d, "proxy.jsonl")) if "dir" in r]
        status_lines = [ln for ln in text.splitlines() if ln.startswith("netgraph: v=")]
        verify(name, spec, rep, proxy, check, status_lines)
        result["pass"] = all(c["ok"] for c in result["checks"])
        return result
    finally:
        for p in reversed(procs):
            try:
                p.send_signal(signal.SIGTERM)
                p.wait(timeout=5)
            except Exception:
                p.kill()
        if not keep:
            shutil.rmtree(os.path.join(d, "base"), ignore_errors=True)


def server_gap_ms(s2c):
    """largest gap the server itself left between consecutive snapshots (drops excluded)"""
    u = [r for r in s2c if r["kind"] == "unrel" and r["action"] == "forward" and r.get("gt") is not None]
    gaps = [(u[i]["t"] - u[i - 1]["t"]) * 1000 for i in range(1, len(u)) if u[i]["seq"] == u[i - 1]["seq"] + 1]
    return max(gaps or [0])


def verify(name, spec, rep, proxy, check, status_lines=()):
    s = rep["session"]
    hitches = rep["hitches"]
    kinds = [h["kind"] for h in hitches]
    # proxy ground truth for what reached the client
    s2c = [r for r in proxy if r["dir"] == "s2c"]
    delivered = [r for r in s2c if r["action"] in ("forward", "forward_reordered", "dup", "release_delay")]
    dropped_unrel = [r for r in s2c if r["kind"] == "unrel" and r["action"] in ("drop", "drop_blackout")]

    # accounting holds in every scenario: the engine saw at most what the proxy delivered
    # (it records only while live, so it may see fewer -- never more)
    d_unrel = sum(1 for r in delivered if r["kind"] == "unrel")
    check(0 < s["in_unreliable_pkts"] <= d_unrel,
          "accounting: client counted %s unreliable in, proxy delivered %d" % (s["in_unreliable_pkts"], d_unrel))

    FRAME, LATE, BOTH, LOSS, STARVED = 1, 2, 3, 4, 5
    if name in ("clean", "clean30"):
        check(s["rec_s"] >= spec["seconds"] - 1.5, "%s: recorded %.1f s of ~%d" % (name, s["rec_s"], spec["seconds"]))
    if name == "clean":
        check(s["missing"] == 0 and s["gaps"] == 0, "clean: no loss (missing %s)" % s["missing"])
        # a loaded test machine can make the local server itself send late; a "ran out of updates"
        # hitch is then correct, as long as the proxy saw the server's own gap
        server_gap = server_gap_ms(s2c)
        unexplained = [h for h in hitches if not (h["kind"] == STARVED and server_gap >= 30)]
        check(not unexplained, "clean: no unexplained hitches (%s; largest server-side gap %.0f ms)" % (kinds, server_gap))
        check(s["late_p99_ms"] is not None and s["late_p99_ms"] < 20, "clean: late p99 %s ms < 20" % s["late_p99_ms"])
    elif name == "drop":
        check(s["missing"] == len(dropped_unrel), "drop: missing %s == proxy drops %d" % (s["missing"], len(dropped_unrel)))
        check(s["gaps"] == 2, "drop: gaps %s == 2" % s["gaps"])
        # the 3-packet drop makes one hitch; on a loaded machine the server's own late send can be the
        # bigger moment in the same 10 s, and hitches that close merge
        sg = server_gap_ms(s2c)
        check(len(hitches) == 1 and (LOSS in kinds or sg >= 30),
              "drop: exactly one hitch, a loss one unless the server itself sent late (%s; server gap %.0f ms)" % (kinds, sg))
    elif name == "delay":
        check(LATE in kinds, "delay: a late hitch (%s)" % kinds)
        h = next((h for h in hitches if h["kind"] == LATE), None)
        if h:
            check(80 <= h["late_ms"] <= 160, "delay: late %.0f ms ~ 120" % h["late_ms"])
            check(h["sentence"].startswith("Updates arrived"), "delay: sentence: %s" % h["sentence"])
        check(s["missing"] == 0, "delay: no loss")
    elif name == "stall":
        check(FRAME in kinds and LATE not in kinds and BOTH not in kinds, "stall: frame hitch only (%s)" % kinds)
        h = next((h for h in hitches if h["kind"] == FRAME), None)
        if h:
            check(h["frame_ms"] >= 150, "stall: frame %.0f >= 150" % h["frame_ms"])
            check("no network delay detected" in h["sentence"], "stall: sentence: %s" % h["sentence"])
    elif name == "dup":
        check(s["dup"] + s["stale"] >= 1, "dup: duplicate seen as dup/stale (%s/%s)" % (s["dup"], s["stale"]))
        check(s["missing"] == 0, "dup: no false loss")
    elif name == "reorder":
        check(s["stale"] >= 1, "reorder: late packet counted stale (%s)" % s["stale"])
        check(s["missing"] == 1, "reorder: one gap position at the early packet (%s)" % s["missing"])
    elif name == "blackout":
        check(STARVED in kinds or LOSS in kinds or LATE in kinds, "blackout: a hitch (%s)" % kinds)
        check(s["starved_ms"] >= 500, "blackout: starved %s ms" % s["starved_ms"])
        check(s["missing"] == len(dropped_unrel), "blackout: outage counted as loss (%s missing, proxy dropped %d)" % (s["missing"], len(dropped_unrel)))
    elif name == "resend":
        c2s_dropped_rel = [r for r in proxy if r["dir"] == "c2s" and r["kind"] == "rel" and r["action"] == "drop"]
        check(len(c2s_dropped_rel) == 1, "resend: proxy dropped one reliable")
        check(s["resent"] >= 1, "resend: client resent %s" % s["resent"])
    elif name == "pause":
        check(not hitches, "pause: no hitches (%s)" % kinds)
    elif name == "clean30":
        check(s["missing"] == 0, "clean30: no loss")
        check(not hitches, "clean30: no hitches at 30 fps (%s)" % kinds)
    elif name == "delay30":
        check(LATE in kinds or BOTH in kinds, "delay30: delay still seen at 30 fps (%s)" % kinds)
        h = next((h for h in hitches if h["kind"] in (LATE, BOTH)), None)
        if h:
            # LATE subtracts the whole read delay (one 33 ms frame here), so it is a lower bound:
            # the held packets were delayed 104-120 ms, minus up to two frames of read cadence
            check(120 - 2 * 34 <= h["late_ms"] <= 160, "delay30: late beyond read delay %.0f ms (true 104-120, frames 33)" % h["late_ms"])
    elif name == "offon":
        # what the pause must not become: a ~2 s frame (other hitches would be the machine's own)
        check(not [h for h in hitches if h["frame_ms"] >= 1000], "offon: no hitch from the 2 s recording pause (%s)" % [h["sentence"] for h in hitches])
        check(spec["seconds"] - 4 <= s["rec_s"] <= spec["seconds"] - 1, "offon: recorded %.1f s excludes the 2 s pause" % s["rec_s"])
        check(s["frames_over_100"] == 0, "offon: the pause became a %s-frame stall" % s["frames_over_100"])
    elif name == "endhitch":
        check(len(hitches) == 1 and hitches[0]["kind"] == FRAME, "endhitch: the stall 1 s before disconnect was saved (%s)" % kinds)
    elif name == "chatsilence":
        quiet = [float(l.split("silence_s=")[1].split()[0]) for l in status_lines if "silence_s=" in l and "silence_s=--" not in l]
        check(quiet and max(quiet) >= 2, "chatsilence: snapshot silence reached %.1f s while chat flowed" % (max(quiet) if quiet else -1))
    elif name == "restart":
        after = [l for l in status_lines if "state=live" in l][-4:]
        check(after and all(" cmdrtt=--" not in l for l in after), "restart: command round trip still measured after the restart")
    elif name == "mapchange":
        check(not hitches, "mapchange: no false hitch from the load (%s)" % kinds)


def traffic_profile(path):
    """per-direction unreliable rate and mean size over gameplay, from the proxy log"""
    recs = [r for r in read_jsonl(path) if r.get("dir") and r.get("gt") is not None and r["gt"] > 2]
    prof = {}
    for d in ("c2s", "s2c"):
        rs = [r for r in recs if r["dir"] == d and r["kind"] == "unrel"]
        if len(rs) < 2:
            return None
        span = rs[-1]["gt"] - rs[0]["gt"]
        prof[d] = (len(rs) / span, sum(r["len"] for r in rs) / len(rs), sum(1 for r in recs if r["dir"] == d and r["kind"] != "unrel"))
    return prof


def compare_passive(on_path, off_path):
    a, b = traffic_profile(on_path), traffic_profile(off_path)
    if not a or not b:
        return False, "passive: not enough traffic to compare"
    parts, ok = [], True
    for d in ("c2s", "s2c"):
        (pa, sa, _), (pb, sb, _) = a[d], b[d]
        rate_ok = abs(pa - pb) / pb < 0.03
        size_ok = abs(sa - sb) <= 1.0
        ok = ok and rate_ok and size_ok
        parts.append("%s %.1f vs %.1f pkt/s, %.1f vs %.1f B" % (d, pa, pb, sa, sb))
    return ok, "passive: recording on vs off: " + "; ".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenarios", nargs="*")
    ap.add_argument("--bin")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--work")
    args = ap.parse_args()

    names = args.scenarios or list(SCENARIOS)
    for n in names:
        if n not in SCENARIOS:
            raise SystemExit("unknown scenario %s (have: %s)" % (n, ", ".join(SCENARIOS)))
    if not os.path.isdir(PAKS):
        raise SystemExit("need %s with pak0.pak/pak1.pak" % PAKS)
    stray = stray_instances()
    if stray:
        raise SystemExit("QSS-M already running (pids %s); close it first" % " ".join(stray))

    binary = args.bin or find_binary()
    st = os.stat(binary)
    work = args.work or tempfile.mkdtemp(prefix="netdiag_")
    log("binary %s (mtime %s)\nwork %s" % (binary, time.ctime(st.st_mtime), work))

    results = []
    for n in names:
        log("== %s" % n)
        r = run_scenario(n, SCENARIOS[n], binary, work, args.keep)
        for c in r["checks"]:
            log("  %s %s" % ("ok  " if c["ok"] else "FAIL", c["what"]))
        results.append(r)
        time.sleep(1)

    by = {r["scenario"]: r for r in results}
    if "clean" in by and "passive_off" in by and by["clean"].get("proxy") and by["passive_off"].get("proxy"):
        ok, what = compare_passive(by["clean"]["proxy"], by["passive_off"]["proxy"])
        by["passive_off"]["checks"].append({"ok": ok, "what": what})
        by["passive_off"]["pass"] = by["passive_off"]["pass"] and ok
        log("== passive\n  %s %s" % ("ok  " if ok else "FAIL", what))

    summary = {"binary": binary, "binary_mtime": st.st_mtime, "work": work,
               "passed": sum(r["pass"] for r in results), "total": len(results), "results": results}
    print(json.dumps(summary, indent=1))
    return 0 if summary["passed"] == summary["total"] else 1


if __name__ == "__main__":
    sys.exit(main())
