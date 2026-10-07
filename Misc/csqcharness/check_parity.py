#!/usr/bin/env python3
"""Check scoped additive blending and clipped one-pixel lines (stdlib only)."""
import argparse
from check_rotpic import read_tga


def check(path, origin=(360, 72), scale=1, engine_overlay=False):
    w, h, rows = read_tga(path)
    ox, oy = origin
    results = []

    def rect(x, y, width, height):
        x, y, width, height = (int(v * scale) for v in (x, y, width, height))
        if x < 0 or y < 0 or x + width > w or y + height > h:
            raise ValueError('parity panel is outside screenshot')
        return [p for line in rows[y:y+height] for p in line[x:x+width]]

    names = ('fill', 'pic', 'subpic', 'character', 'rawstring', 'string',
             'rotsubpic', 'line')
    for row, name in enumerate(names):
        cells = [rect(ox + col*32, oy + row*32, 24, 24) for col in range(4)]
        ref, add, after, omitted = cells
        # Compare actual drawing, not just untouched background. Font glyphs
        # have transparent texels and their colour comes from the Quake atlas.
        active = [i for i, p in enumerate(ref) if p[0] > 10]
        results.append((name + '/visible', bool(active)))
        results.append((name + '/explicit-normal-restored',
                        max(abs(a-b) for p, q in zip(ref, after) for a,b in zip(p,q)) <= 2))
        results.append((name + '/omitted-flags-normal',
                        max(abs(a-b) for p, q in zip(ref, omitted) for a,b in zip(p,q)) <= 2))
        results.append((name + '/additive-preserves-blue', bool(active) and
                        all(abs(add[i][2]-128) <= 2 and add[i][2]-ref[i][2] >= 40
                            for i in active)))
        results.append((name + '/additive-source-colour', bool(active) and
                        all(abs(add[i][0]-ref[i][0]) <= 2 for i in active)))
        if name == 'line':
            # Width argument is 9, but FTE's line still occupies one physical
            # raster row at both UI scales.
            stride = 24 * scale
            lit_rows = {i // stride for i in active}
            results.append(('line/one-physical-pixel', len(lit_rows) == 1))

    y = oy + 268
    results.append(('clip/line-visible',
                    any(p[0] > 200 for p in rect(ox+8, y+6, 8, 4))))
    results.append(('clip/left-unmodified',
                    all(p[0] == 0 for p in rect(ox, y, 8, 24))))
    results.append(('clip/right-unmodified',
                    all(p[0] == 0 for p in rect(ox+16, y, 8, 24))))
    results.append(('clip/fill-additive',
                    all(abs(p[0]-128) <= 2 and abs(p[2]-128) <= 2
                        for p in rect(ox+8, y+12, 8, 8))))
    reference = rect(ox+32, y+4, 16, 16)
    results.append(('clip/reset-and-blend-restored',
                    all(abs(p[0]-128) <= 2 and abs(p[2]-64) <= 2 for p in reference)))
    for col in (2,3):
        candidate = rect(ox+col*32, y+4, 16, 16)
        results.append(('blend/reserved-mode-%d-normal' % col, candidate == reference))
    if engine_overlay:
        # Full QC paints the frame black and prints STATE through the engine.
        # Gray font texels in this strip prove engine drawing escaped the QC clip.
        results.append(('clip/engine-overlay-not-clipped',
                        any(min(p) > 80 and max(p)-min(p) <= 3
                            for line in rows[h//4:h//2] for p in line[w//2-40:w//2+40])))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('screenshot')
    ap.add_argument('--origin', nargs=2, type=int, default=(360, 72))
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument("--engine-overlay", action="store_true")
    args = ap.parse_args()
    results = check(args.screenshot, args.origin, args.scale, args.engine_overlay)
    for name, ok in results:
        print('%s %s' % ('PASS' if ok else 'FAIL', name))
    passed = sum(ok for _, ok in results)
    print('%d/%d checks passed' % (passed, len(results)))
    return passed != len(results)


if __name__ == '__main__':
    raise SystemExit(main())
