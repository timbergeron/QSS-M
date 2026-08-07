#!/usr/bin/env python3
"""Verify a rotpic-sample screenshot.

Reads the uncompressed 24bpp TGA that QSS-M's `screenshot tga` writes and
checks the test grid drawn by rotpic_tests.qc:

  * drawrotpic at angle 0 must match drawpic exactly
  * angle 360 must match angle 0
  * drawrotsubpic over the full texture must match drawpic
  * angles 90/180/270 must equal the reference cell rotated by that much
  * -90 must equal 270
  * drawrotsubpic over a sub-rect must match drawsubpic
  * a tinted/alpha'd rotpic must match the same tint through drawpic

Pure stdlib on purpose: the machine that runs this has no numpy/PIL.
"""

import sys

CELL = 64
GAP = 8
GX = 16
GY = 72

# how far apart two 64x64 cells may be before we call it a failure.
# GL rasterises the two paths as a quad and as a triangle fan, so allow a
# hair of slack rather than demanding bit equality.
MAX_ABS = 2
MAX_MEAN = 0.05


def read_tga(path):
    d = open(path, "rb").read()
    idlen, cmaptype, imgtype = d[0], d[1], d[2]
    w = d[12] | (d[13] << 8)
    h = d[14] | (d[15] << 8)
    bpp = d[16]
    descriptor = d[17]
    if imgtype != 2 or cmaptype != 0 or bpp != 24:
        raise SystemExit("%s: not an uncompressed 24bpp TGA (type=%d bpp=%d)"
                         % (path, imgtype, bpp))
    off = 18 + idlen
    px = d[off:off + w * h * 3]
    if len(px) < w * h * 3:
        raise SystemExit("%s: truncated pixel data" % path)
    top_down = bool(descriptor & 0x20)
    # rows[y] is a row of (r,g,b) tuples with y=0 at the TOP of the screen
    rows = []
    for y in range(h):
        src = y if top_down else (h - 1 - y)
        base = src * w * 3
        row = [(px[base + x * 3 + 2], px[base + x * 3 + 1], px[base + x * 3])
               for x in range(w)]
        rows.append(row)
    return w, h, rows


def cell(rows, w, h, col, row):
    x0 = GX + col * (CELL + GAP)
    y0 = GY + row * (CELL + GAP)
    if x0 + CELL > w or y0 + CELL > h:
        raise SystemExit("cell (%d,%d) at %d,%d falls outside the %dx%d "
                         "screenshot - run at a larger resolution"
                         % (col, row, x0, y0, w, h))
    return [rows[y0 + y][x0:x0 + CELL] for y in range(CELL)]


def rot90(a):     # clockwise on screen
    return [[a[CELL - 1 - x][y] for x in range(CELL)] for y in range(CELL)]


def rot180(a):
    return [[a[CELL - 1 - y][CELL - 1 - x] for x in range(CELL)] for y in range(CELL)]


def rot270(a):    # counter-clockwise on screen
    return [[a[x][CELL - 1 - y] for x in range(CELL)] for y in range(CELL)]


def diff(a, b):
    worst = 0
    total = 0
    for y in range(CELL):
        ra, rb = a[y], b[y]
        for x in range(CELL):
            pa, pb = ra[x], rb[x]
            for c in range(3):
                d = abs(pa[c] - pb[c])
                total += d
                if d > worst:
                    worst = d
    return worst, total / float(CELL * CELL * 3)


def is_blank(a):
    first = a[0][0]
    for row in a:
        for p in row:
            if p != first:
                return False
    return True


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_rotpic.py <screenshot.tga>")
    w, h, rows = read_tga(sys.argv[1])

    c = lambda col, row: cell(rows, w, h, col, row)

    ref = c(0, 0)          # drawpic
    sub_ref = c(0, 2)      # drawsubpic, top-left quadrant
    tint_ref = c(0, 3)     # drawpic, tinted + 50% alpha

    if is_blank(ref):
        raise SystemExit("reference cell is a flat colour - the test pic did "
                         "not load, or the panel is not on screen")

    checks = [
        ("drawrotpic angle 0      == drawpic",        c(1, 0), ref),
        ("drawrotpic angle 360    == angle 0",        c(2, 0), c(1, 0)),
        ("drawrotsubpic full tex  == drawpic",        c(3, 0), ref),
        ("drawrotpic angle 90     == rot90(ref)",     c(0, 1), rot90(ref)),
        ("drawrotpic angle 180    == rot180(ref)",    c(1, 1), rot180(ref)),
        ("drawrotpic angle 270    == rot270(ref)",    c(2, 1), rot270(ref)),
        ("drawrotpic angle -90    == angle 270",      c(3, 1), c(2, 1)),
        ("drawrotsubpic q0        == drawsubpic q0",  c(1, 2), sub_ref),
        ("drawrotsubpic q0 at 90  == rot90(subref)",  c(2, 2), rot90(sub_ref)),
        ("drawrotpic tint/alpha   == drawpic same",   c(1, 3), tint_ref),
        ("drawrotpic corner pivot == drawpic",        c(2, 3), ref),
    ]

    failed = 0
    for name, got, want in checks:
        worst, mean = diff(got, want)
        ok = worst <= MAX_ABS and mean <= MAX_MEAN
        if not ok:
            failed += 1
        print("%-4s %-42s maxdiff=%-4d meandiff=%.4f"
              % ("PASS" if ok else "FAIL", name, worst, mean))

    # the q3 sub-rect must NOT match q0, otherwise the txmin argument is ignored
    worst, mean = diff(c(3, 2), sub_ref)
    distinct = worst > MAX_ABS
    if not distinct:
        failed += 1
    print("%-4s %-42s maxdiff=%-4d meandiff=%.4f"
          % ("PASS" if distinct else "FAIL",
             "drawrotsubpic q3        != q0 (txmin used)", worst, mean))

    print("\n%d/%d checks passed" % (len(checks) + 1 - failed, len(checks) + 1))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
