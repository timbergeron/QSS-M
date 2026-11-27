#!/usr/bin/env python3
"""Compare the DIFF lines that src/fte_diff.qc printed under QSS-M and FTEQW.

    1. build ftediff.dat (src/ftediff.src) and install it as <gamedir>/csprogs.dat
    2. run each engine with +map start and capture its stdout:
         QSS-M ... -basedir B -game G +map start > qss.log
         fteqw ... -basedir B -game G -nohome +map start > fte.log
    3. python3 diff_fte.py qss.log fte.log [--allow known_differences.txt]

Exit status is non-zero if any value differs that is not listed in the allow file
(one check name per line, # comments). Checks missing from either log count as
differences, so a probe that crashed part-way cannot read as parity.
"""
import argparse
import re
import sys


def load(path):
    vals, done, repeated = {}, False, set()
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\r\n')
            m = re.search(r'DIFF (\S+) ?(.*)$', line)
            if line.endswith('DIFF-DONE'):
                done = True
            elif m:
                name, value = m.groups()
                if name in vals:
                    repeated.add(name)
                vals[name] = value
    return vals, done, repeated


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('qss')
    ap.add_argument('fte')
    ap.add_argument('--allow', help='file of check names whose difference is intended')
    a = ap.parse_args()

    allow = {}
    if a.allow:
        for line in open(a.allow):
            name, _, why = line.partition('#')
            if name.strip():
                allow[name.strip()] = why.strip()

    q, qdone, qrepeated = load(a.qss)
    f, fdone, frepeated = load(a.fte)
    bad = 0
    for tag, values, done, repeated in (('qss', q, qdone, qrepeated), ('fte', f, fdone, frepeated)):
        if not done or not values:
            print('INCOMPLETE %s log (needs DIFF values and DIFF-DONE)' % tag)
            bad += 1
        for name in sorted(repeated):
            print('DUPLICATE %s %s' % (tag, name))
            bad += 1
    for name in sorted(set(q) | set(f)):
        if name not in q or name not in f:
            print('MISSING  %-40s in %s' % (name, 'qss' if name not in q else 'fte'))
            bad += 1
            continue
        qv, fv = q[name], f[name]
        if qv == fv:
            continue
        if name in allow:
            print('ALLOWED  %-40s qss=%r fte=%r  (%s)' % (name, qv, fv, allow[name]))
        else:
            print('DIFFERS  %-40s qss=%r fte=%r' % (name, qv, fv))
            bad += 1
    same = sum(1 for n in q if f.get(n) == q[n])
    print('\n%d identical, %d unexplained differences' % (same, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
