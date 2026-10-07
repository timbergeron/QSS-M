#!/usr/bin/env python3
"""
udpproxy.py -- NetQuake fault-injecting UDP proxy with ground-truth log  // woods #netgraph

Sits between one client and one server. Applies a fault schedule and writes a
JSON-lines log of every datagram: time, direction, length, NetQuake class and
sequence, and what was done to it. The netgraph runner compares the engine's
report against this log.

The server answers a connect with CCREP_ACCEPT carrying its own game port, and
the client then talks to that port directly -- around the proxy. So the proxy
rewrites that port field to its own.

Usage:
  udpproxy.py --listen 26200 --server 127.0.0.1:26100 --log proxy.jsonl --faults faults.json

faults.json is a list; times are seconds after the first server->client
unreliable datagram (gameplay traffic):
  {"type": "drop",     "dir": "s2c", "at": 15, "count": 3}       next N unreliable datagrams
  {"type": "delay",    "dir": "s2c", "at": 15, "ms": 100}        hold everything for ms, then release in order
  {"type": "dup",      "dir": "s2c", "at": 15}                   send the next unreliable twice
  {"type": "reorder",  "dir": "s2c", "at": 15}                   swap the next two unreliables
  {"type": "blackout", "dir": "both","at": 15, "ms": 3000}       drop everything
  {"type": "drop_rel", "dir": "c2s", "at": 15}                   drop the next reliable data datagram once
"""

import argparse
import heapq
import json
import select
import socket
import struct
import sys
import time

NETFLAG_LENGTH_MASK = 0x0000FFFF
NETFLAG_DATA = 0x00010000
NETFLAG_ACK = 0x00020000
NETFLAG_EOM = 0x00080000
NETFLAG_UNRELIABLE = 0x00100000
NETFLAG_CTL = 0x80000000
CCREP_ACCEPT = 0x81


def classify(data):
    if len(data) < 4:
        return "short", None
    hdr = struct.unpack(">I", data[:4])[0]
    if hdr == 0xFFFFFFFF:
        return "oob", None
    if hdr & NETFLAG_CTL:
        return "ctl", None
    if len(data) < 8:
        return "short", None
    seq = struct.unpack(">I", data[4:8])[0]
    if hdr & NETFLAG_UNRELIABLE:
        return "unrel", seq
    if hdr & NETFLAG_ACK:
        return "ack", seq
    if hdr & NETFLAG_DATA:
        return "rel", seq
    return "other", seq


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", type=int, required=True)
    ap.add_argument("--server", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--faults", default=None)
    ap.add_argument("--duration", type=float, default=600)
    args = ap.parse_args()

    shost, sport = args.server.rsplit(":", 1)
    server = (shost, int(sport))
    faults = []
    if args.faults:
        with open(args.faults) as f:
            faults = json.load(f)
    for fl in faults:
        fl["done"] = False
        fl.setdefault("dir", "s2c")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", args.listen))
    # NAT-style: one upstream socket per client source port. The client probes
    # from more than one socket while connecting (NetQuake connect plus a DP/FTE
    # challenge), and each reply must go back to the socket that asked.
    ups = {}        # client addr -> upstream socket
    owner = {}      # upstream socket -> client addr

    def upstream(addr):
        u = ups.get(addr)
        if u is None:
            u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            u.bind(("127.0.0.1", 0))
            ups[addr] = u
            owner[u] = addr
        return u

    log = open(args.log, "w", buffering=1)
    start = time.monotonic()
    game_t0 = None
    queue = []          # (release_time, order, dir, data, client addr)
    order = 0
    hold_until = {"s2c": 0.0, "c2s": 0.0}
    reorder_slot = {"s2c": None, "c2s": None}

    def now():
        return time.monotonic()

    def rel_t():
        return None if game_t0 is None else now() - game_t0

    def emit(direction, data, action, extra=None):
        kind, seq = classify(data)
        rec = {"t": round(now() - start, 6), "gt": None if game_t0 is None else round(now() - game_t0, 6),
               "dir": direction, "len": len(data), "kind": kind, "seq": seq, "action": action}
        if extra:
            rec.update(extra)
        log.write(json.dumps(rec) + "\n")

    def deliver(direction, data, peer):
        if direction == "s2c":
            sock.sendto(data, peer)
        else:
            upstream(peer).sendto(data, server)

    def active(fl, direction):
        if fl["done"]:
            return False
        if fl["dir"] not in (direction, "both"):
            return False
        t = rel_t()
        return t is not None and t >= fl["at"]

    def handle(direction, data, peer):
        nonlocal order
        kind, seq = classify(data)

        # rewrite the accept port so the client keeps talking to us
        if direction == "s2c" and kind == "ctl" and len(data) >= 9 and data[4] == CCREP_ACCEPT:
            old = struct.unpack("<i", data[5:9])[0]
            data = data[:5] + struct.pack("<i", args.listen) + data[9:]
            emit(direction, data, "rewrite_accept", {"old_port": old})
            deliver(direction, data, peer)
            return

        for fl in faults:
            if not active(fl, direction):
                continue
            typ = fl["type"]
            if typ == "blackout_unrel":	# snapshots only; reliables (chat) still pass
                if rel_t() < fl["at"] + fl["ms"] / 1000.0:
                    if kind == "unrel":
                        emit(direction, data, "drop_blackout")
                        return
                    continue
                fl["done"] = True
            elif typ == "blackout":
                if rel_t() < fl["at"] + fl["ms"] / 1000.0:
                    emit(direction, data, "drop_blackout")
                    return
                fl["done"] = True
            elif typ == "drop" and kind == "unrel":
                fl.setdefault("left", fl.get("count", 1))
                fl["left"] -= 1
                if fl["left"] <= 0:
                    fl["done"] = True
                emit(direction, data, "drop")
                return
            elif typ == "drop_rel" and kind == "rel":
                fl["done"] = True
                emit(direction, data, "drop")
                return
            elif typ == "dup" and kind == "unrel":
                fl["done"] = True
                emit(direction, data, "forward")
                deliver(direction, data, peer)
                emit(direction, data, "dup")
                deliver(direction, data, peer)
                return
            elif typ == "reorder" and kind == "unrel":
                if reorder_slot[direction] is None:
                    reorder_slot[direction] = data
                    emit(direction, data, "hold_reorder")
                    return
                held = reorder_slot[direction]
                reorder_slot[direction] = None
                fl["done"] = True
                emit(direction, data, "forward")
                deliver(direction, data, peer)
                emit(direction, held, "forward_reordered")
                deliver(direction, held, peer)
                return
            elif typ == "delay":
                if not fl.get("armed"):
                    fl["armed"] = True
                    hold_until[direction] = now() + fl["ms"] / 1000.0
                fl["done"] = True

        if now() < hold_until[direction]:
            order += 1
            heapq.heappush(queue, (hold_until[direction], order, direction, data, peer))
            emit(direction, data, "hold_delay")
            return

        emit(direction, data, "forward")
        deliver(direction, data, peer)

    deadline = start + args.duration
    while now() < deadline:
        timeout = 0.05
        if queue:
            timeout = max(0.0, min(timeout, queue[0][0] - now()))
        r, _, _ = select.select([sock] + list(owner), [], [], timeout)
        for s in r:
            data, addr = s.recvfrom(65535)
            if s is sock:
                handle("c2s", data, addr)
            else:
                if game_t0 is None and classify(data)[0] == "unrel":
                    game_t0 = now()
                    log.write(json.dumps({"t": round(now() - start, 6), "event": "game_t0"}) + "\n")
                handle("s2c", data, owner[s])
        while queue and queue[0][0] <= now():
            _, _, direction, data, peer = heapq.heappop(queue)
            emit(direction, data, "release_delay")
            deliver(direction, data, peer)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
