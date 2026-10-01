"""Malformed alias models must be rejected or raise a recoverable error, never kill the process.

The MDL loader used to Sys_Error (hard exit) on a bad version, zero frames, zero vertices, zero
triangles, an invalid skin count or too many frames.  Each case mutates one header field of
progs/flame.mdl (small, loaded by `map start`'s precache), then loads the map twice: the second
load must not touch a half-built model.  Pass = the engine is still alive afterwards.

    QSSM_BIN=/path/to/stress-enabled/QSS-M python3 test_mdl_reject.py
"""
import struct, sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qssm_stress as S

# mdl_t: ident, version, scale[3], translate[3], boundingradius, eyeposition[3] = 48 bytes, then
OFS = {"version": 4, "numskins": 48, "numverts": 60, "numtris": 64, "numframes": 68}
CASES = {
    "wrong version": ("version", 99),
    "zero skins": ("numskins", 0),
    "too many skins": ("numskins", 100000),
    "zero verts": ("numverts", 0),
    "zero tris": ("numtris", 0),
    "zero frames": ("numframes", 0),
}


def main():
    cfg = S.Config(binary=S.find_binary(), paks=S.find_paks(), registered=True,
                   results="/tmp/qssm-mdlreject", scenarios=[], runs=1, minutes=0, seed=0,
                   iterations=1, round_robin=False, allow_net=False, nosound=True,
                   boot_timeout=90, map_timeout=60, hang_timeout=20, hang_retries=0,
                   port_base=29800, rcon_password="x", replay_pace=0.05,
                   always_check=True, keep=False)
    pak = cfg.paks[0]
    ents = {n: (o, l) for n, o, l in S.pak_entries(pak)}
    ofs, ln = ents["progs/flame.mdl"]
    orig = S.pak_read(pak, ofs, ln)
    r = S.Runner(cfg)
    failures = []
    for name, (field, val) in [("pristine control", (None, None))] + list(CASES.items()):
        eng = r.make_engine("mdl")
        d = eng.work / "base" / "id1" / "progs"
        d.mkdir(parents=True, exist_ok=True)
        buf = bytearray(orig)
        if field:
            struct.pack_into("<i", buf, OFS[field], val)
        (d / "flame.mdl").write_bytes(bytes(buf))
        eng.start()
        eng.wait_ready(timeout=45)
        for _ in range(2):	# second load re-references the (now invalid) model
            eng.send("map start")
            time.sleep(2.5)
        alive = True
        try:
            eng.status(timeout=15)
        except Exception:
            alive = False
        rc = eng.returncode()
        ok = alive and rc is None
        print(f"{'ok  ' if ok else 'FAIL'} {name:18s} alive={alive} rc={rc}")
        if not ok:
            failures.append(name)
        eng.kill()
    print("FAILED: " + ", ".join(failures) if failures else "all cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
