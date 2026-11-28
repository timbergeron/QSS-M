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


# Mission-pack scenarios: (name, wad, status-bar state forced by chudss.dat).
# items holds bits 0-22, items2 bits 23-31 (QSS-M sends items | items2 << 23).
MISSIONPACK_SCENARIOS = [
    # laser cannon active; grenade launcher + proximity gun overlap; wetsuit,
    # empathy shields, sigil 1; Hipnotic's keys on the status row
    ("hip1", "hipnotic", dict(items=1 | 2 | 16 | 128 | 65536 | 256 | 131072 | 262144,
                              items2=1 | 2 | 4 | 32, weapon=1 << 23, armor=0)),
    # mjolnir active, no grenade launcher (proximity gun drawn alone), green armour
    ("hip2", "hipnotic", dict(items=1 | 4 | 128 | 65536 | 256 | 8192,
                              items2=0, weapon=128, armor=100)),
    # lava nailgun active (powered icon, r_invbar1), lava nails, shield, antigrav, Rogue armour 2
    ("rogue1", "rogue", dict(items=1 | 4 | 4096 | 65536,
                             items2=2 | 8 | 64 | 128, weapon=4096, armor=150)),
    # shotgun active (r_invbar2), multi weapons, plasma ammo, Rogue armour 3
    ("rogue2", "rogue", dict(items=1 | 16384 | 32768,
                             items2=4 | 16, weapon=1, armor=200)),
    # plasma gun active (powered icon), multi rockets, Rogue armour 1
    ("rogue3", "rogue", dict(items=1 | 65536,
                             items2=1 | 32, weapon=65536, armor=50)),
]


def scenario_cfg(state):
    lines = ["set chud_ss_%s %d" % (k, v) for k, v in state.items()]
    lines += ["set chud_ss_ammo 50", "set chud_ss_shells 25", "set chud_ss_nails 30",
              "set chud_ss_rockets 40", "set chud_ss_cells 50"]
    return "\n".join(lines) + "\n"


def install(args, game, csprogs, cfg, progs=None, wad=None):
    gamedir = os.path.join(args.basedir, game)
    os.makedirs(gamedir, exist_ok=True)
    shutil.copyfile(csprogs, os.path.join(gamedir, "csprogs.dat"))
    for name, src in (("progs.dat", progs), (os.path.join("gfx.wad"), wad)):
        dst = os.path.join(gamedir, name)
        if src:
            shutil.copyfile(src, dst)
        elif os.path.exists(dst):
            os.remove(dst)
    with open(os.path.join(gamedir, "chud_compare.cfg"), "w") as f:
        f.write(cfg)


def compare_preset(args, label, hud_game, ref_game, preset, hud_extra=()):
    tag = "%s%d" % (label, preset)
    ref_path = shoot(args, ref_game, preset, os.path.join(args.out, "ref_%s.log" % tag))
    hud_path = shoot(args, hud_game, preset, os.path.join(args.out, "hud_%s.log" % tag), hud_extra)
    shutil.copyfile(ref_path, os.path.join(args.out, "ref_%s.tga" % tag))
    shutil.copyfile(hud_path, os.path.join(args.out, "hud_%s.tga" % tag))
    _, _, ref = read_tga(ref_path)
    _, _, hud = read_tga(hud_path)

    diff = [[(255, 0, 255) if max(abs(a[c] - b[c]) for c in range(3)) > TOLERANCE else
             tuple(v // 3 for v in b) for a, b in zip(ra, rb)] for ra, rb in zip(ref, hud)]
    write_tga(os.path.join(args.out, "diff_%s.tga" % tag), diff)

    failed = 0
    for name, (x0, y0, x1, y1) in REGIONS[preset].items():
        bad = sum(1 for y in range(y0, y1) for x in range(x0, x1)
                  if diff[y][x] == (255, 0, 255))
        total = (x1 - x0) * (y1 - y0)
        failed += bad != 0
        print("%s  %-7s preset %d  %-12s %5d / %5d pixels differ"
              % ("PASS" if bad == 0 else "FAIL", label, preset, name, bad, total))
    return failed


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
    ap.add_argument("--missionpacks", metavar="PAK0",
                    help="also run the Hipnotic/Rogue scenarios with placeholder "
                         "gfx.wad files built from this id1 pak0.pak")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    hud_dat, ref_dat = os.path.join(HERE, "csprogs.dat"), os.path.join(HERE, "ref.dat")
    for dat in (hud_dat, ref_dat):
        if not os.path.exists(dat):
            raise SystemExit("missing %s -- run src/build.sh first" % dat)
    presets = [int(p) for p in args.presets.split(",")]

    install(args, "chud", hud_dat, COMMON_CFG)
    install(args, "chudref", ref_dat, COMMON_CFG)
    failed = 0
    for preset in presets:
        failed += compare_preset(args, "id1", "chud", "chudref", preset, args.hud_arg)

    if args.missionpacks:
        ss_dat = os.path.join(HERE, "chudss.dat")
        if not os.path.exists(ss_dat):
            raise SystemExit("missing %s -- build.sh needs src/qsssextensions.qc" % ss_dat)
        wads = os.path.join(args.out, "placeholder_wads")
        subprocess.run([sys.executable, os.path.join(HERE, "make_placeholder_wads.py"),
                        args.missionpacks, wads], check=True)
        # A loose gfx/inv_laser.tga must not switch the sample to Hipnotic: the
        # engine only looks inside gfx.wad, and needs the pack's whole lump set.
        for game, dat in (("chud_loose", hud_dat), ("chudref_loose", ref_dat)):
            install(args, game, dat, COMMON_CFG)
            os.makedirs(os.path.join(args.basedir, game, "gfx"), exist_ok=True)
            write_tga(os.path.join(args.basedir, game, "gfx", "inv_laser.tga"),
                      [[(255, 0, 0)] * 24 for _ in range(16)])
        for preset in presets:
            failed += compare_preset(args, "loose", "chud_loose", "chudref_loose", preset)

        for label, pack, state in MISSIONPACK_SCENARIOS:
            cfg = COMMON_CFG + scenario_cfg(state)
            wad = os.path.join(wads, pack, "gfx.wad")
            install(args, "chud_" + label, hud_dat, cfg, ss_dat, wad)
            install(args, "chudref_" + label, ref_dat, cfg, ss_dat, wad)
            for preset in presets:
                failed += compare_preset(args, label, "chud_" + label, "chudref_" + label, preset)

    print("artifacts in", args.out)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
