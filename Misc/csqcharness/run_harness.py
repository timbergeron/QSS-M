#!/usr/bin/env python3
"""Run the QSS-M CSQC/MenuQC harness and report pass/fail.

Three layers:

  1. QC assertions  -- selftest.qc + the per-module halves print SELFTEST lines
                       which are parsed here.
  2. Engine-side    -- pr_dumpplatform output is checked for per-target builtin
                       numbers and for rejection of a mixed -Tcs -Tmenu run.
  3. Pixel checks   -- check_rotpic.py diffs the drawrotpic/drawrotsubpic grid.

Pure stdlib; the machine this was written on has no numpy/PIL.
"""

import argparse
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# builtin numbers that must differ per target -- these are what regressed when
# pr_dumpplatform emitted documentednumber for every module.
DUMP_EXPECT = {
    'qscsextensions.qc':   {'drawfill': 323, 'drawpic': 322, 'drawsubpic': 328},
    'qsmenuextensions.qc': {'drawfill': 457, 'drawpic': 456, 'drawsubpic': 469,
                            'gettime': 67, 'registercvar': 42, 'findflags': 87,
                            'tokenize': 58, 'buf_del': 441,
                            'serverkey': 354, 'serverkeyfloat': 0,
                            'getmodelindex': 200, 'frameforname': 276,
                            'frameduration': 277, 'frametoname': 0},
}


def run_engine(binary, basedir, game, args, timeout, capture=True):
    cmd = [binary, '-basedir', basedir, '-game', game,
           '-window', '-width', '800', '-height', '600'] + args
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        return p.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode('utf-8', 'replace') if e.stdout else ''
        return out + "\n*** engine did not exit within %ss ***\n" % timeout


def parse_selftest(out, module):
    """Return (passes, failures, ran) from SELFTEST lines."""
    fails = re.findall(r'^SELFTEST FAIL (.*)$', out, re.M)
    passes = re.findall(r'^SELFTEST PASS (.*)$', out, re.M)
    done = re.search(r'^SELFTEST DONE %s pass=(\d+) fail=(\d+)$' % module, out, re.M)
    return passes, fails, done


def check_dump(binary, basedir, game):
    """Regenerate the platform defs and assert the per-target numbers."""
    srcdir = os.path.join(basedir, game, 'src')
    cfg = os.path.join(basedir, game, 'hq_dump.cfg')
    with open(cfg, 'w') as f:
        f.write('pr_dumpplatform -Tcs -O qscsextensions\n'
                'pr_dumpplatform -Tmenu -O qsmenuextensions\n'
                'pr_dumpplatform -Tcs -Tmenu -O hq_mixed\n'
                'pr_dumpplatform -Tmenu\n'   # default filename, no -O
                'quit\n')
    out = run_engine(binary, basedir, game, ['+exec', 'hq_dump.cfg'], 120)
    os.remove(cfg)

    results = []
    # the mixed invocation must be refused rather than emitting one bad file
    mixed = os.path.join(srcdir, 'hq_mixed.qc')
    refused = 'cannot be combined' in out and not os.path.exists(mixed)
    results.append(('dump/mixed-target-refused', refused))
    if os.path.exists(mixed):
        os.remove(mixed)

    for fname, expect in DUMP_EXPECT.items():
        path = os.path.join(srcdir, fname)
        if not os.path.exists(path):
            results.append(('dump/%s-written' % fname, False))
            continue
        text = open(path, encoding='utf-8', errors='replace').read()
        for name, num in expect.items():
            m = re.search(r'\)\s*%s = #(\d+);' % re.escape(name), text)
            got = int(m.group(1)) if m else None
            results.append(('dump/%s/%s=#%d' % (fname.split('.')[0], name, num),
                            got == num))
    # -Tmenu must get its own filename, and its guard must reject the other
    # modules rather than MENU itself (a caller pre-defining MENU used to #error)
    mn_text = open(os.path.join(srcdir, 'qsmenuextensions.qc'), encoding='utf-8',
                   errors='replace').read()
    results.append(('dump/menu-guard-rejects-csqc-not-menu',
                    '#if defined(QUAKEWORLD) || defined(CSQC) || defined(SSQC)' in mn_text))
    results.append(('dump/menu-default-filename',
                    os.path.exists(os.path.join(srcdir, 'qsmenuextensions.qc'))
                    and not os.path.exists(os.path.join(srcdir, 'qsextensions.qc'))))

    # compatibility aliases stay bound at load but must not be advertised
    for alias in ('gettime_legacy', 'drawsubpic_legacy', 'buf_del_legacy',
                  'chr2str_menuqc', 'stringtokeynum_menuqc'):
        results.append(('dump/%s-not-advertised' % alias,
                        not re.search(r'\)\s*%s = #' % alias, mn_text)))

    # the signature fix that let the generated header compile at all
    cs = open(os.path.join(srcdir, 'qscsextensions.qc'), encoding='utf-8',
              errors='replace').read()
    results.append(('dump/gettimed-uses-__double',
                    '__double(optional int timetype) gettimed' in cs))
    mn = open(os.path.join(srcdir, 'qsmenuextensions.qc'), encoding='utf-8',
              errors='replace').read()
    results.append(('dump/registercvar-has-flags',
                    'string defaultvalue, optional float flags) registercvar' in mn))
    results.append(('dump/getkeybind-has-optional-bindmap',
                    'float keynum, optional float bindmap) getkeybind' in mn))
    results.append(('dump/getresolution-forfullscreen-optional',
                    'float mode, optional float forfullscreen) getresolution' in mn))
    return results


def check_archived_cvar(basedir, game):
    """registercvar's DP archive bit must actually reach the config."""
    cfg = os.path.join(basedir, game, 'config.cfg')
    if not os.path.exists(cfg):
        return [('cvar/archive-persists', False)]
    # the config carries high-bit quake charset bytes, so read it as bytes
    data = open(cfg, 'rb').read()
    ok = b'seta hq_archived' in data
    absent = b'hq_bogus' not in data      # no flags -> must not be archived
    return [('cvar/archive-persists', ok),
            ('cvar/unflagged-not-archived', absent)]


def strip_test_cvars(basedir, game):
    cfg = os.path.join(basedir, game, 'config.cfg')
    if not os.path.exists(cfg):
        return
    data = open(cfg, 'rb').read()
    keep = [l for l in data.split(b'\n') if not l.startswith(b'seta hq_')]
    open(cfg, 'wb').write(b'\n'.join(keep))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', required=True, help='path to the QSS-M executable')
    ap.add_argument('--basedir', required=True)
    ap.add_argument('--game', default='csqcharness')
    ap.add_argument('--map', default='start')
    ap.add_argument('--timeout', type=float, default=90)
    ap.add_argument('--skip-pixels', action='store_true')
    args = ap.parse_args()

    results = []
    # any earlier run -- including a manual one -- may have persisted the
    # archived probe cvar, which would make registercvar report "already exists"
    strip_test_cvars(args.basedir, args.game)
    shots = os.path.join(args.basedir, args.game, 'screenshots')
    for f in os.listdir(shots) if os.path.isdir(shots) else []:
        if f.endswith('.tga'):
            os.remove(os.path.join(shots, f))

    # ---- csqc: assertions + the drawrotpic grid, in one run
    out = run_engine(args.bin, args.basedir, args.game,
                     ['+hq_selftest', '1', '+rotpic_autoshot', '1',
                      '+map', args.map], args.timeout)
    p, f, done = parse_selftest(out, 'csqc')
    results += [('csqc/' + n, True) for n in p] + [('csqc/' + n, False) for n in f]
    results.append(('csqc/selftest-completed', done is not None))
    for bad in ('unimplemented builtin', 'Program error', 'Host_Error'):
        results.append(('csqc/no "%s"' % bad, bad not in out))

    csqc_shot = newest_tga(shots)

    # ---- menuqc: assertions only (no map); the grid is checked in its own run
    out = run_engine(args.bin, args.basedir, args.game,
                     ['+hq_selftest', '1', '+togglemenu', '1'], args.timeout)
    p, f, done = parse_selftest(out, 'menuqc')
    results += [('menuqc/' + n, True) for n in p] + [('menuqc/' + n, False) for n in f]
    results.append(('menuqc/selftest-completed', done is not None))
    for bad in ('unimplemented builtin', 'Program error', 'Host_Error'):
        results.append(('menuqc/no "%s"' % bad, bad not in out))

    results += check_archived_cvar(args.basedir, args.game)
    strip_test_cvars(args.basedir, args.game)

    # ---- engine-side generator checks
    results += check_dump(args.bin, args.basedir, args.game)

    # ---- pixel grid
    if not args.skip_pixels and csqc_shot:
        rc = subprocess.run([sys.executable, os.path.join(HERE, 'check_rotpic.py'),
                             csqc_shot], stdout=subprocess.PIPE)
        text = rc.stdout.decode()
        m = re.search(r'(\d+)/(\d+) checks passed', text)
        results.append(('pixels/rotpic-grid', bool(m) and m.group(1) == m.group(2)))
        if not (m and m.group(1) == m.group(2)):
            print(text)
    elif not args.skip_pixels:
        results.append(('pixels/rotpic-grid', False))

    failed = [n for n, ok in results if not ok]
    for n, ok in results:
        print("%-6s %s" % ("PASS" if ok else "FAIL", n))
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


def newest_tga(shots):
    if not os.path.isdir(shots):
        return None
    files = [os.path.join(shots, f) for f in os.listdir(shots) if f.endswith('.tga')]
    return max(files, key=os.path.getmtime) if files else None


if __name__ == '__main__':
    sys.exit(main())
