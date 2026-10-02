"""Tree-structure BSP preflight: each lie must be rejected, never crash.

Mod_BSPTreeInvalid (gl_model.c) covers what Mod_BSPIndicesInvalid does not:
leaf contents / marksurface ranges, marksurface faces, node face ranges, node
cycles, clipnode children and submodel headnode / face ranges.  The original
finding was a fuzzed leafs lump whose garbage contents made SV_FindTouchedLeafs
walk a leaf as if it were a node (SIGSEGV).

Each case mutates one field of a pristine maps/start.bsp (BSP29), writes it as
a loose maps/zz_mut.bsp and loads it.  Pass = the engine survives AND logs the
preflight warning, i.e. the map was rejected rather than half-loaded.
"""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import qssm_stress as S

LUMP_NODES, LUMP_FACES, LUMP_CLIPNODES, LUMP_LEAFS, LUMP_MARKSURFACES, LUMP_MODELS = 5, 7, 9, 10, 11, 14
LEAF, NODE, CLIP = 28, 24, 8


def lump(buf, i):
    return struct.unpack_from("<II", buf, 4 + 8 * i)


def mutations(orig):
    """name -> (mutator, expected warning fragment)"""
    def poke(fmt, lumpidx, rel, val):
        def f(b):
            ofs, _ = lump(b, lumpidx)
            struct.pack_into(fmt, b, ofs + rel, val)
        return f
    return {
        "leaf contents >= 0": (poke("<i", LUMP_LEAFS, 1 * LEAF, 0), "invalid contents"),
        "leaf contents < -31": (poke("<i", LUMP_LEAFS, 1 * LEAF, -1000), "invalid contents"),
        "leaf marksurface range": (poke("<H", LUMP_LEAFS, 1 * LEAF + 22, 0xffff), "out of range marksurfaces"),
        "marksurface face": (poke("<H", LUMP_MARKSURFACES, 0, 0xffff), "out of range face"),
        "node face range": (poke("<H", LUMP_NODES, 20, 0xffff), "out of range faces"),
        "node self cycle": (poke("<H", LUMP_NODES, 4, 0), "loops back"),
        # no BSP29 clipnode case: a 16-bit child >= count wraps to a (valid) negative contents value, so
        # only BSP2 clipnodes can carry an out of range child.
        "submodel headnode0": (poke("<i", LUMP_MODELS, 36, 0x7ffffff), "out of range headnode"),
        "submodel clip headnode": (poke("<i", LUMP_MODELS, 40, 0x7ffffff), "out of range clip headnode"),
        "submodel faces": (poke("<i", LUMP_MODELS, 36 + 16 + 8, 0x7ffffff), "out of range faces"),
        # not a preflight rejection: the map loads but has no worldspawn, so the QC never precaches
        # anything and the spawning player's weapon model used to Sys_Error in SV_ModelIndex
        "empty entity lump (must load)": (lambda b: struct.pack_into("<I", b, 8, 0), None),
    }


# must-load cases (want None) that must also log a specific line, proving the
# fixed path actually ran rather than the map quietly failing some other way
MUST_LOAD_LOG = {
    "empty entity lump (must load)": "SV_ModelIndex: model",
}


def main():
    cfg = S.Config(binary=S.find_binary(), paks=S.find_paks(), registered=True,
                   results="/tmp/qssm-bsptree", scenarios=[], runs=1, minutes=0, seed=0,
                   iterations=1, round_robin=False, allow_net=False, nosound=True,
                   boot_timeout=90, map_timeout=60, hang_timeout=20, hang_retries=0,
                   port_base=29600, rcon_password="x", replay_pace=0.05,
                   always_check=True, keep=False)
    pak = cfg.paks[0]
    ents = {n: (o, l) for n, o, l in S.pak_entries(pak)}
    ofs, ln = ents["maps/start.bsp"]
    orig = S.pak_read(pak, ofs, ln)
    assert struct.unpack_from("<i", orig, 0)[0] == 29

    r = S.Runner(cfg)
    failures = []

    # control: the pristine map, renamed, must still load
    cases = [("pristine control", None, None)]
    cases += [(n, m, w) for n, (m, w) in mutations(orig).items()]
    for name, mut, want in cases:
        eng = r.make_engine("tree")
        d = eng.work / "base" / "id1" / "maps"
        d.mkdir(parents=True, exist_ok=True)
        buf = bytearray(orig)
        if mut:
            mut(buf)
        (d / "zz_mut.bsp").write_bytes(bytes(buf))
        eng.start()
        eng.wait_ready(timeout=45)
        eng.send("map zz_mut")
        time.sleep(3.0)
        alive = True
        st = None
        try:
            # a must-load case has to reach a spawned client, not merely survive
            deadline = time.time() + 20
            while True:
                st = eng.status(timeout=15)
                if want is not None or st.get("signon") == "4" or time.time() > deadline:
                    break
                time.sleep(1.0)
        except Exception:
            alive = False
        signon = st.get("signon") if st else None
        eng.read_log()
        log = open(eng.work / "console.log", errors="replace").read()
        warned = want is not None and want in log and "zz_mut.bsp has" in log
        rc = eng.returncode()
        if want is None:
            expect = MUST_LOAD_LOG.get(name)
            ok = (alive and signon == "4" and "zz_mut.bsp has" not in log
                  and (expect is None or expect in log))
        else:
            ok = alive and warned
        print(f"{'ok  ' if ok else 'FAIL'} {name:30s} alive={alive} rc={rc} signon={signon} warned={warned}")
        if not ok:
            failures.append(name)
        eng.kill()

    print()
    print("FAILED: " + ", ".join(failures) if failures else "all cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
