#!/usr/bin/env python3
"""Pixel-compare the csqchud sample against the engine's own status bar.

For each scr_sbar preset this runs the engine twice on the same map:
  <basedir>/chudref  ref.dat as csprogs.dat: full CSQC that lets the engine
                     draw its status bar (VF_DRAWENGINESBAR)
  <basedir>/chud     csprogs.dat: the sample HUD (CSQC_DrawHud only)
Both give themselves everything with impulse 9, wait out the pickup flash,
screenshot and quit. The HUD rectangles of the two shots are then diffed.

    python3 Misc/csqchud/compare.py --bin <QSS-M> --basedir <throwaway basedir>

The basedir needs id1/pak0.pak + pak1.pak (and qssm.pak at its root). Run
src/build.sh first. Exit status is non-zero if any rectangle differs.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 800, 600
TOLERANCE = 8          # per-channel difference ignored (GL rounding)

# HUD rectangles (x0, y0, x1, y1) at 800x600, scr_sbarscale 1, per preset.
REGIONS = {
    1: {"sbar+ibar": (240, 552, 560, 600)},
    2: {"status": (240, 576, 560, 600),
        "items": (432, 560, 560, 576),
        "qw column": (756, 364, 800, 552)},
    3: {"armor": (18, 539, 122, 564),
        "face+health": (18, 563, 122, 588),
        "ammo": (678, 564, 784, 588),
        "keys+sigils": (704, 544, 784, 560),
        "qw column": (735, 295, 779, 483)},
}

# Written to <gamedir>/chud_compare.cfg and exec'd: Quake keeps only the first
# 50 command-line arguments (MAX_NUM_ARGVS), and anything past that, +map
# included, is silently dropped.
COMMON_CFG = """set chud_autotest 1
scr_sbarscale 1
scr_sbaralpha 1
viewsize 100
gl_clear 1
scr_sbarfacecolor 0
crosshair 0
scr_clock 0
scr_showfps 0
scr_ping 0
con_notifytime 0
cl_demoreel 0
scr_sbarshowqeammo 1
scr_sbaralphaqwammo 1
r_showlocs 0
scr_showspeed 0
scr_movekeys 0
scr_csqc_cleanhud 0
"""
# pinned above so a config.cfg left by an earlier manual run can't leak
# overlays (r_showlocs draws lines across the view) into the shots


def read_tga(path):
    d = open(path, "rb").read()
    idlen, cmaptype, imgtype = d[0], d[1], d[2]
    w = d[12] | (d[13] << 8)
    h = d[14] | (d[15] << 8)
    if imgtype != 2 or cmaptype != 0 or d[16] != 24:
        raise SystemExit("%s: not an uncompressed 24bpp TGA" % path)
    off = 18 + idlen
    px = d[off:off + w * h * 3]
    top_down = bool(d[17] & 0x20)
    rows = []
    for y in range(h):
        base = (y if top_down else h - 1 - y) * w * 3
        rows.append([(px[base + x * 3 + 2], px[base + x * 3 + 1], px[base + x * 3])
                     for x in range(w)])
    return w, h, rows


def write_tga(path, rows):
    h, w = len(rows), len(rows[0])
    out = bytearray(18)
    out[2] = 2
    out[12], out[13], out[14], out[15], out[16], out[17] = w & 255, w >> 8, h & 255, h >> 8, 24, 0x20
    for row in rows:
        for r, g, b in row:
            out += bytes((b, g, r))
    open(path, "wb").write(out)


def shoot(args, game, preset, log, extra=()):
    gamedir = os.path.join(args.basedir, game)
    shots = os.path.join(gamedir, "screenshots")
    shutil.rmtree(shots, ignore_errors=True)
    cmd = [args.bin, "-basedir", args.basedir, "-game", game, "-window",
           "-width", str(W), "-height", str(H), "-noquakeimport",
           "+scr_sbar", str(preset), "+exec", "chud_compare.cfg"] + list(extra) + ["+map", args.map]
    with open(log, "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=args.timeout)
    found = sorted(glob.glob(os.path.join(shots, "*.tga")))
    if not found:
        raise SystemExit("%s preset %d: no screenshot (see %s)" % (game, preset, log))
    return found[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--basedir", required=True)
    ap.add_argument("--map", default="start")
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--out", default=os.path.join(HERE, "artifacts"))
    ap.add_argument("--presets", default="1,2,3")
    ap.add_argument("--hud-arg", action="append", default=[],
                    help="extra console args for the sample run only, e.g. "
                         "--hud-arg=+set --hud-arg=chud_ibar_ofs --hud-arg='1 -24' "
                         "(a sanity check that a moved widget is caught)")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    for game, dat in (("chud", "csprogs.dat"), ("chudref", "ref.dat")):
        src = os.path.join(HERE, dat)
        if not os.path.exists(src):
            raise SystemExit("missing %s -- run src/build.sh first" % src)
        os.makedirs(os.path.join(args.basedir, game), exist_ok=True)
        shutil.copyfile(src, os.path.join(args.basedir, game, "csprogs.dat"))
        with open(os.path.join(args.basedir, game, "chud_compare.cfg"), "w") as f:
            f.write(COMMON_CFG)

    failed = 0
    for preset in [int(p) for p in args.presets.split(",")]:
        ref_path = shoot(args, "chudref", preset, os.path.join(args.out, "ref%d.log" % preset))
        hud_path = shoot(args, "chud", preset, os.path.join(args.out, "hud%d.log" % preset), args.hud_arg)
        shutil.copyfile(ref_path, os.path.join(args.out, "ref%d.tga" % preset))
        shutil.copyfile(hud_path, os.path.join(args.out, "hud%d.tga" % preset))
        _, _, ref = read_tga(ref_path)
        _, _, hud = read_tga(hud_path)

        diff = [[(255, 0, 255) if max(abs(a[c] - b[c]) for c in range(3)) > TOLERANCE else
                 tuple(v // 3 for v in b) for a, b in zip(ra, rb)] for ra, rb in zip(ref, hud)]
        write_tga(os.path.join(args.out, "diff%d.tga" % preset), diff)

        for name, (x0, y0, x1, y1) in REGIONS[preset].items():
            bad = sum(1 for y in range(y0, y1) for x in range(x0, x1)
                      if diff[y][x] == (255, 0, 255))
            total = (x1 - x0) * (y1 - y0)
            status = "PASS" if bad == 0 else "FAIL"
            failed += bad != 0
            print("%s  preset %d  %-12s %5d / %5d pixels differ" % (status, preset, name, bad, total))
    print("artifacts in", args.out)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
