"""Minimal deterministic repro: one BSP lump offset, one crash.

Quake BSP header is version(4) then 15 lumps of (fileofs, filelen).  The
loaders do `mod_base + l->fileofs` with no validation, so a single negative or
out-of-range offset points the parse anywhere.
"""
import struct, sys, time
sys.path.insert(0, "/Users/timbergeron/codedev/QSS-M/Misc/stress")
import qssm_stress as S

LUMPS = ["entities", "planes", "textures", "vertexes", "visibility", "nodes",
         "texinfo", "faces", "lighting", "clipnodes", "leafs", "marksurfaces",
         "edges", "surfedges", "models"]

cfg = S.Config(binary=S.find_binary(), paks=S.find_paks(), registered=True,
               results="/tmp/qssm-bsplump", scenarios=[], runs=1, minutes=0, seed=0,
               iterations=1, round_robin=False, allow_net=False, nosound=True,
               boot_timeout=90, map_timeout=60, hang_timeout=20, hang_retries=0,
               port_base=29400, rcon_password="x", replay_pace=0.05,
               always_check=True, keep=True)

pak = cfg.paks[0]
ents = {n: (o, l) for n, o, l in S.pak_entries(pak)}
SRC = "maps/start.bsp"
ofs, ln = ents[SRC]
orig = S.pak_read(pak, ofs, ln)
print(f"seed {SRC}: {ln} bytes, version {struct.unpack('<i', orig[:4])[0]}")

# which lump index -> which loader, and the two classic bad values
cases = []
for idx in range(15):
    cases.append((idx, 0x80000000, "negative"))
cases.append((3, len(orig) * 4, "beyond eof"))

r = S.Runner(cfg)
crashed = []
for idx, val, why in cases:
    eng = r.make_engine(f"lump{idx}")
    d = eng.work / "base" / "id1" / "maps"
    d.mkdir(parents=True, exist_ok=True)
    buf = bytearray(orig)
    struct.pack_into("<I", buf, 4 + idx * 8, val & 0xffffffff)   # lump[idx].fileofs
    (d / "start.bsp").write_bytes(bytes(buf))

    dead = None
    booted = True
    eng.start()
    try:
        eng.wait_ready(timeout=45)
    except Exception as exc:
        booted = False
        dead = "BOOT FAILED: " + str(exc)[:50]
    if booted:
        try:
            eng.send("map start")
            time.sleep(2.5)
            eng.status(timeout=15)
        except Exception as exc:
            dead = str(exc)[:60]
    rc = eng.returncode()
    ips = r.crashwatch.poll(wait=6 if (rc is not None and rc < 0) else 0.5,
                            pid=eng.proc.pid if eng.proc else None)
    frame = ""
    if ips:
        info = S.CrashReports.summarize(ips[-1])
        eframes = [f for f in info["frames"] if "QSS-M`" in f]
        frame = f"{info['signal']}: {eframes[0] if eframes else '?'}"
        crashed.append((idx, LUMPS[idx], why, frame))
    print(f"lump[{idx:2d}] {LUMPS[idx]:14s} {why:11s} -> "
          f"{frame if frame else ('survived' if not dead else 'lost: ' + dead)}")
    eng.kill()

print()
print(f"{len(crashed)}/{len(cases)} lump offsets crash the engine")
for idx, name, why, frame in crashed:
    print(f"   lump[{idx}] {name} ({why}): {frame}")
