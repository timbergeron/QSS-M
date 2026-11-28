#!/usr/bin/env python3
"""Build placeholder Hipnotic / Rogue gfx.wad files for compare.py.

The engine picks its mission-pack status bar purely from which lumps gfx.wad
has (Sbar_LoadPics), and the sample HUD detects it the same way. So a copy of
id1's gfx.wad plus every Hipnotic or Rogue lump name is enough to exercise both
layouts without the commercial mission-pack data.

Each placeholder is an id1 pic of the same size with its palette indices
shifted by a per-lump amount, so every lump looks distinct: drawing the wrong
lump in the wrong slot shows up as a pixel difference, not a lucky match.

    make_placeholder_wads.py <id1 pak0.pak> <outdir>
writes <outdir>/hipnotic/gfx.wad and <outdir>/rogue/gfx.wad.
"""
import os
import struct
import sys

TYPE_QPIC = 0x42


def read_pak_file(pak, wanted):
    d = open(pak, "rb").read()
    _, ofs, ln = struct.unpack("<4sii", d[:12])
    for i in range(ln // 64):
        name, o, l = struct.unpack("<56sii", d[ofs + i * 64:ofs + i * 64 + 64])
        if name.split(b"\0")[0].decode("latin-1") == wanted:
            return d[o:o + l]
    raise SystemExit("%s: no %s" % (pak, wanted))


def read_wad(data):
    magic, n, ofs = struct.unpack("<4sii", data[:12])
    if magic != b"WAD2":
        raise SystemExit("not a WAD2 file")
    lumps = []
    for i in range(n):
        filepos, disksize, size, typ, comp, _, name = struct.unpack(
            "<iiibbh16s", data[ofs + i * 32:ofs + i * 32 + 32])
        lumps.append((name.split(b"\0")[0].decode("latin-1").lower(), typ, data[filepos:filepos + disksize]))
    return lumps


def write_wad(path, lumps):
    body = bytearray()
    infos = []
    for name, typ, blob in lumps:
        infos.append((12 + len(body), len(blob), typ, name))
        body += blob
        while len(body) % 4:
            body += b"\0"
    out = bytearray(struct.pack("<4sii", b"WAD2", len(lumps), 12 + len(body)))
    out += body
    for filepos, size, typ, name in infos:
        out += struct.pack("<iiibbh16s", filepos, size, size, typ, 0, 0, name.encode()[:15])
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(out)


def shifted(qpic, offset):
    """Same pic, palette indices moved by offset (fullbrights and transparency kept)."""
    w, h = struct.unpack("<ii", qpic[:8])
    px = bytearray(qpic[8:8 + w * h])
    for i, p in enumerate(px):
        if p < 224:
            px[i] = (p + offset) % 224
    return qpic[:8] + bytes(px)


def build(base, extra):
    pics = {name: blob for name, typ, blob in base if typ == TYPE_QPIC}
    lumps = list(base)
    for name, (src, offset) in extra.items():
        lumps.append((name, TYPE_QPIC, shifted(pics[src], offset)))
    return lumps


def hipnotic_lumps():
    out = {}
    # five hipnotic weapons, each over a different id weapon so they stay tellable apart
    sources = {"laser": "nailgun", "mjolnir": "rlaunch", "gren_prox": "srlaunch",
               "prox_gren": "snailgun", "prox": "sshotgun"}
    for k, (name, src) in enumerate(sources.items()):
        offset = 16 * (k + 1)
        out["inv_" + name] = ("inv_" + src, offset)
        out["inv2_" + name] = ("inv2_" + src, offset)
        for f in range(1, 6):
            out["inva%d_%s" % (f, name)] = ("inva%d_%s" % (f, src), offset)
    out["sb_wsuit"] = ("sb_suit", 16)
    out["sb_eshld"] = ("sb_invuln", 32)
    return out


def rogue_lumps():
    return {
        "r_invbar1": ("ibar", 16), "r_invbar2": ("ibar", 48),
        "r_lava": ("inv_nailgun", 32), "r_superlava": ("inv_snailgun", 32),
        "r_gren": ("inv_rlaunch", 32), "r_multirock": ("inv_srlaunch", 32),
        "r_plasma": ("inv_lightng", 32),
        "r_shield1": ("sb_invuln", 48), "r_agrav1": ("sb_invis", 48),
        "r_teambord": ("disc", 16),
        "r_ammolava": ("sb_nails", 64), "r_ammomulti": ("sb_rocket", 64),
        "r_ammoplasma": ("sb_cells", 64),
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    base = read_wad(read_pak_file(sys.argv[1], "gfx.wad"))
    write_wad(os.path.join(sys.argv[2], "hipnotic", "gfx.wad"), build(base, hipnotic_lumps()))
    write_wad(os.path.join(sys.argv[2], "rogue", "gfx.wad"), build(base, rogue_lumps()))


if __name__ == "__main__":
    main()
