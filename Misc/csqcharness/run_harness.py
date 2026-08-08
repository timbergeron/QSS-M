#!/usr/bin/env python3
"""Run the QSS-M CSQC/MenuQC harness and report pass/fail.

Closed loop, in this order, so a stale artifact cannot produce a false pass:

    generate both extension headers with the binary under test
        -> validate the headers
        -> compile menu.dat + csprogs.dat with fteqcc
        -> run the semantic suites
        -> run the pixel suite

That chain proves three things together: the engine emits correct declarations,
stock fteqcc accepts them, and the resulting bytecode runs on that same engine.
Skipping the compile step (no --fteqcc) is allowed but reported as a failure,
because then only the last link is under test.

Pure stdlib; the machine this was written on has no numpy/PIL.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import threading
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

# compatibility aliases: bound at load, but must never be advertised
DUMP_ABSENT = ('gettime_legacy', 'drawsubpic_legacy', 'buf_del_legacy',
               'chr2str_menuqc', 'stringtokeynum_menuqc')

FORBIDDEN = ('unimplemented builtin', 'Program error', 'Host_Error',
             'AddressSanitizer', 'runtime error:', 'Assertion failed')


class RunResult(object):
    def __init__(self, command, stdout, returncode, timed_out, duration):
        self.command = command
        self.stdout = stdout
        self.returncode = returncode
        self.timed_out = timed_out
        self.duration = duration


def sha256(path):
    if not path or not os.path.exists(path):
        return None
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def run(cmd, timeout, logpath, cwd=None):
    t0 = time.time()
    timed_out = False
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, cwd=cwd)
        out, rc = p.stdout, p.returncode
    except subprocess.TimeoutExpired as e:
        out, rc, timed_out = (e.stdout or b''), None, True
    text = out.decode('utf-8', 'replace')
    if logpath:
        with open(logpath, 'w') as f:
            f.write(' '.join(cmd) + '\n\n' + text)
    return RunResult(cmd, text, rc, timed_out, time.time() - t0)


def run_with_trigger(cmd, timeout, logpath, marker, action):
    """run(), but fire `action` the moment `marker` shows up on stdout.

    A handshake rather than a delay: the engine fflushes after every line
    (Ded_WriteOutput in sys_sdl_unix.c), so this observes the QC's own progress
    and cannot stage a file before the QC has asserted its absence, however slow
    a debug-build startup or map load turns out to be. Returns (result, fired).
    """
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    state = {'timed_out': False, 'fired': False}

    def expire():
        state['timed_out'] = True
        p.kill()

    # the read loop below only notices time passing when a line arrives, so the
    # deadline needs its own thread or a silent hang would block forever
    watchdog = threading.Timer(timeout, expire)
    watchdog.start()
    chunks = []
    try:
        for line in p.stdout:
            chunks.append(line)
            if not state['fired'] and marker.encode() in line:
                state['fired'] = True
                action()
        rc = p.wait()
    finally:
        watchdog.cancel()
        watchdog.join()

    text = b''.join(chunks).decode('utf-8', 'replace')
    if logpath:
        with open(logpath, 'w') as f:
            f.write(' '.join(cmd) + '\n\n' + text)
    return (RunResult(cmd, text, None if state['timed_out'] else rc,
                      state['timed_out'], time.time() - t0),
            state['fired'])


def check_process(tag, r, results):
    """Strict subprocess validation -- a crash must not read as a pass."""
    results.append(('%s/no-timeout' % tag, not r.timed_out))
    if not r.timed_out:
        results.append(('%s/exit-status-zero' % tag, r.returncode == 0))
        results.append(('%s/not-killed-by-signal' % tag, r.returncode >= 0))
    for bad in FORBIDDEN:
        results.append(('%s/no "%s"' % (tag, bad), bad not in r.stdout))


def parse_selftest(r, module, tag, results):
    passes = re.findall(r'^SELFTEST PASS (.*)$', r.stdout, re.M)
    fails = re.findall(r'^SELFTEST FAIL (.*)$', r.stdout, re.M)
    done = re.search(r'^SELFTEST DONE %s pass=(\d+) fail=(\d+)$' % module,
                     r.stdout, re.M)

    for n in passes:
        results.append(('%s/%s' % (tag, n), True))
    for n in fails:
        results.append(('%s/%s' % (tag, n.split(' got=')[0]), False))

    results.append(('%s/selftest-completed' % tag, done is not None))
    if done:
        # the module's own tally must agree with what we parsed, or lines were
        # lost or duplicated between the engine and here
        results.append(('%s/counts-reconcile' % tag,
                        int(done.group(1)) == len(passes) and
                        int(done.group(2)) == len(fails)))
    names = list(passes) + [n.split(' got=')[0] for n in fails]
    results.append(('%s/no-duplicate-test-names' % tag,
                    len(names) == len(set(names))))
    results.append(('%s/ran-some-checks' % tag, len(names) > 0))


def check_headers(srcdir, results):
    for fname, expect in DUMP_EXPECT.items():
        path = os.path.join(srcdir, fname)
        short = fname.split('.')[0]
        if not os.path.exists(path):
            results.append(('dump/%s-written' % short, False))
            continue
        results.append(('dump/%s-written' % short, True))
        text = open(path, encoding='utf-8', errors='replace').read()
        for name, num in sorted(expect.items()):
            m = re.search(r'\)\s*%s = #(\d+);' % re.escape(name), text)
            results.append(('dump/%s/%s=#%d' % (short, name, num),
                            m is not None and int(m.group(1)) == num))

    mn_path = os.path.join(srcdir, 'qsmenuextensions.qc')
    cs_path = os.path.join(srcdir, 'qscsextensions.qc')
    mn = open(mn_path, encoding='utf-8', errors='replace').read() if os.path.exists(mn_path) else ''
    cs = open(cs_path, encoding='utf-8', errors='replace').read() if os.path.exists(cs_path) else ''

    for alias in DUMP_ABSENT:
        results.append(('dump/%s-not-advertised' % alias,
                        bool(mn) and not re.search(r'\)\s*%s = #' % alias, mn)))
    results.append(('dump/menu-guard-rejects-csqc-not-menu',
                    '#if defined(QUAKEWORLD) || defined(CSQC) || defined(SSQC)' in mn))
    results.append(('dump/menu-default-filename',
                    os.path.exists(mn_path) and
                    not os.path.exists(os.path.join(srcdir, 'qsextensions.qc'))))
    results.append(('dump/gettimed-uses-__double',
                    '__double(optional int timetype) gettimed' in cs))
    results.append(('dump/registercvar-has-flags',
                    'string defaultvalue, optional float flags) registercvar' in mn))
    results.append(('dump/getkeybind-has-optional-bindmap',
                    'float keynum, optional float bindmap) getkeybind' in mn))
    results.append(('dump/getresolution-forfullscreen-optional',
                    'float mode, optional float forfullscreen) getresolution' in mn))

    # The constant blocks are emitted without newlines, so one trailing //
    # comment silently comments out every constant after it on that line.
    # VF_ACTIVESEAT carries such a comment, and it used to take VF_AFOV, the
    # VF_SCREEN*SIZE pair, all of RF_*, and all four PRECACHE_PIC_* with it --
    # they were in the file but unreachable, and referencing one was a compile
    # error rather than anything the engine would report.
    for name in ('VF_AFOV', 'VF_SCREENPSIZE', 'RF_VIEWMODEL',
                 'PRECACHE_PIC_FROMWAD', 'PRECACHE_PIC_TEST'):
        results.append(('dump/%s-not-commented-out' % name,
                        bool(re.search(r'^[^\n/]*const float %s =' % name,
                                       cs, re.M))))


def strip_test_cvars(basedir, game):
    """Any earlier run -- including a manual one -- may have persisted our probe
    cvars, which would make registercvar report "already exists"."""
    cfg = os.path.join(basedir, game, 'config.cfg')
    if not os.path.exists(cfg):
        return
    data = open(cfg, 'rb').read()
    keep = [l for l in data.split(b'\n') if not l.startswith(b'seta hq_')]
    open(cfg, 'wb').write(b'\n'.join(keep))


def check_archived_cvar(basedir, game):
    cfg = os.path.join(basedir, game, 'config.cfg')
    if not os.path.exists(cfg):
        return [('cvar/archive-persists', False)]
    # the config carries high-bit quake charset bytes, so read it as bytes
    data = open(cfg, 'rb').read()
    return [('cvar/archive-persists', b'seta hq_archived' in data),
            ('cvar/unflagged-not-archived', b'hq_bogus' not in data)]


# ---------------------------------------------------------------- fixtures
#
# The picture tests need real image files on disk, but a binary blob in the
# tree is a thing that rots quietly. Generating them means the expected
# dimensions in pic_tests.qc and the bytes they are checked against come from
# one place, and it makes the mid-run staging in the recovery check just
# another call to the same writer.

def write_tga(path, w, h, pixel):
    """Uncompressed 24bpp TGA -- what Image_LoadTGA accepts (type 2, 24 or 32
    bit) and what check_rotpic.py reads. pixel(x, y) returns (r,g,b), y=0 top."""
    header = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    w & 0xff, w >> 8, h & 0xff, h >> 8, 24, 0])
    rows = []
    for y in range(h - 1, -1, -1):      # no 0x20 in the descriptor, so bottom-up
        row = bytearray()
        for x in range(w):
            r, g, b = pixel(x, y)
            row += bytes((b, g, r))
        rows.append(bytes(row))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # via a temporary sibling: the recovery check has the engine polling this
    # exact name every frame, and it must never observe the file mid-creation.
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(header + b''.join(rows))
    os.replace(tmp, path)


def quadrants(w, h):
    """Four distinct colours, so a rotation or a sub-rect that picked the wrong
    corner shows up as a pixel difference rather than matching by symmetry."""
    def pixel(x, y):
        left, top = x < w // 2, y < h // 2
        if top:
            return (220, 40, 40) if left else (40, 200, 40)
        return (50, 90, 230) if left else (230, 210, 40)
    return pixel


def grid_pixel(x, y):
    """As above plus an off-centre marker, so no 90-degree rotation of the
    image can be mistaken for the image itself."""
    base = quadrants(64, 64)(x, y)
    if 8 <= x < 20 and 6 <= y < 14:
        return (255, 255, 255)
    return base


# name -> (w, h, pixel fn). Sizes are asserted in pic_tests.qc; keep them in step.
FIXTURES = {
    'hq_direct.tga': (32, 16, quadrants(32, 16)),
    'hq_grid.tga':   (64, 64, grid_pixel),
    # only the omitted-flags check may name this one. that check asks whether a
    # load happened at all, so it is vacuous against an already-cached image --
    # and every other fixture is cached by the time it runs.
    'hq_defaultarg.tga': (48, 12, quadrants(48, 12)),
}
LATE_FIXTURE = ('hq_late.tga', 24, 8, quadrants(24, 8))


def stage_fixtures(gamedir):
    written = []
    for name, (w, h, fn) in FIXTURES.items():
        p = os.path.join(gamedir, 'gfx', name)
        write_tga(p, w, h, fn)
        written.append(p)
    return written


def newest_tga(shots):
    if not os.path.isdir(shots):
        return None
    files = [os.path.join(shots, f) for f in os.listdir(shots) if f.endswith('.tga')]
    return max(files, key=os.path.getmtime) if files else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', required=True, help='the QSS-M executable under test')
    ap.add_argument('--basedir', required=True)
    ap.add_argument('--game', default='csqcharness')
    ap.add_argument('--fteqcc', help='rebuild the progs from the freshly dumped '
                                     'headers before running (closed loop)')
    ap.add_argument('--map', default='start')
    ap.add_argument('--timeout', type=float, default=90)
    ap.add_argument('--artifacts', default=os.path.join(HERE, 'artifacts'))
    ap.add_argument('--skip-pixels', action='store_true')
    args = ap.parse_args()

    gamedir = os.path.join(args.basedir, args.game)
    srcdir = os.path.join(gamedir, 'src')
    art = args.artifacts
    os.makedirs(art, exist_ok=True)
    results = []

    def engine(a, log):
        return run([args.bin, '-basedir', args.basedir, '-game', args.game,
                    '-window', '-width', '800', '-height', '600'] + a,
                   args.timeout, os.path.join(art, log))

    # ---- 1. generate the headers with the binary under test
    for stale in ('qscsextensions.qc', 'qsmenuextensions.qc', 'qsextensions.qc'):
        p = os.path.join(srcdir, stale)
        if os.path.exists(p):
            os.remove(p)
    cfg = os.path.join(gamedir, 'hq_dump.cfg')
    with open(cfg, 'w') as f:
        f.write('pr_dumpplatform -Tcs -O qscsextensions\n'
                'pr_dumpplatform -Tmenu -O qsmenuextensions\n'
                'pr_dumpplatform -Tcs -Tmenu -O hq_mixed\n'
                'pr_dumpplatform -Tmenu\n'
                'quit\n')
    r = engine(['+exec', 'hq_dump.cfg'], 'dump.log')
    os.remove(cfg)
    check_process('dump', r, results)
    mixed = os.path.join(srcdir, 'hq_mixed.qc')
    results.append(('dump/mixed-target-refused',
                    'cannot be combined' in r.stdout and not os.path.exists(mixed)))
    if os.path.exists(mixed):
        os.remove(mixed)

    # ---- 2. validate them
    check_headers(srcdir, results)

    # ---- 3. compile against exactly those headers
    if args.fteqcc:
        b = run([os.path.join(srcdir, 'build.sh'), args.fteqcc], args.timeout,
                os.path.join(art, 'build.log'), cwd=srcdir)
        results.append(('build/fteqcc-exit-zero', b.returncode == 0))
        results.append(('build/both-progs-written',
                        b.stdout.count('Compile finished') == 2))
        results.append(('build/no-warnings', b.stdout.count('Done. 0 warnings') == 2))
        results.append(('build/no-errors', 'error' not in b.stdout.lower()))
    else:
        results.append(('build/progs-rebuilt-this-run (pass --fteqcc)', False))

    strip_test_cvars(args.basedir, args.game)
    shots = os.path.join(gamedir, 'screenshots')
    if os.path.isdir(shots):
        for f in os.listdir(shots):
            if f.endswith('.tga'):
                os.remove(os.path.join(shots, f))

    # the picture fixtures, and the absence of the one the recovery check
    # stages later -- a leftover from an interrupted run would let that check
    # pass without ever exercising a retry
    fixtures = stage_fixtures(gamedir)
    late = os.path.join(gamedir, 'gfx', LATE_FIXTURE[0])
    if os.path.exists(late):
        os.remove(late)

    # ---- 4. semantic suites, one per engine state
    r = engine(['+hq_csqc_selftest', '1', '+rotpic_autoshot', '1',
                '+map', args.map], 'csqc.log')
    check_process('csqc', r, results)
    parse_selftest(r, 'csqc', 'csqc', results)
    shot = newest_tga(shots)

    strip_test_cvars(args.basedir, args.game)
    r = engine(['+hq_menu_selftest', '1', '+togglemenu', '1'],
               'menu-disconnected.log')
    check_process('menu-disconnected', r, results)
    parse_selftest(r, 'menuqc', 'menu-disconnected', results)

    # menuqc with a map loaded: constate must flip to active, and the model
    # handles must still be ours rather than cl.model_precache's
    strip_test_cvars(args.basedir, args.game)
    r = engine(['+hq_menu_selftest', '1', '+hq_expect_connected', '1',
                '+map', args.map, '+togglemenu', '1'], 'menu-connected.log')
    check_process('menu-connected', r, results)
    parse_selftest(r, 'menuqc', 'menu-connected', results)

    results += check_archived_cvar(args.basedir, args.game)
    strip_test_cvars(args.basedir, args.game)

    # ---- 4b. failed-load cleanup, on its own because it ends in Sys_Error if
    # the cleanup leaks a slot, and that would take the rest of a shared run
    # with it rather than reporting one failure
    r = engine(['+hq_pic_stress', '1', '+map', args.map], 'pic-stress.log')
    check_process('pic-stress', r, results)
    parse_selftest(r, 'csqc', 'pic-stress', results)
    strip_test_cvars(args.basedir, args.game)

    # ---- 4c. same-process recovery: stage the file only once the qc has said
    # it is absent, then let the qc poll for it. A second engine run cannot test
    # this -- exit clears both cache layers, so a fresh process passes however
    # badly the first attempt poisoned them.
    #
    # The trigger matches the check's name, not PASS or FAIL, so an engine that
    # gets the initial assertion wrong still reaches the retry checks instead of
    # stalling until the qc's own budget runs out.
    name, lw, lh, lfn = LATE_FIXTURE
    r, staged = run_with_trigger(
        [args.bin, '-basedir', args.basedir, '-game', args.game,
         '-window', '-width', '800', '-height', '600',
         '+hq_pic_recovery', '1', '+map', args.map],
        args.timeout, os.path.join(art, 'pic-recovery.log'),
        'pic/late-absent-not-cached',
        lambda: write_tga(late, lw, lh, lfn))
    results.append(('pic-recovery/handshake-fired', staged))
    results.append(('pic-recovery/fixture-was-staged', os.path.exists(late)))
    check_process('pic-recovery', r, results)
    parse_selftest(r, 'csqc', 'pic-recovery', results)
    strip_test_cvars(args.basedir, args.game)

    for p in fixtures + [late]:
        if os.path.exists(p):
            os.remove(p)

    # ---- 5. pixels
    if not args.skip_pixels:
        if shot:
            rc = run([sys.executable, os.path.join(HERE, 'check_rotpic.py'), shot],
                     args.timeout, os.path.join(art, 'pixels.log'))
            m = re.search(r'(\d+)/(\d+) checks passed', rc.stdout)
            ok = bool(m) and m.group(1) == m.group(2)
            results.append(('pixels/rotpic-grid', ok))
            if not ok:
                print(rc.stdout)
        else:
            results.append(('pixels/screenshot-produced', False))

    # ---- report
    failed = [n for n, ok in results if not ok]
    for n, ok in results:
        print("%-6s %s" % ("PASS" if ok else "FAIL", n))

    summary = {
        'passed': len(results) - len(failed),
        'total': len(results),
        'failures': failed,
        'compiled_this_run': bool(args.fteqcc),
        'engine': {
            'path': os.path.abspath(args.bin),
            'sha256': sha256(args.bin),
            'mtime': os.path.getmtime(args.bin) if os.path.exists(args.bin) else None,
        },
        'inputs': {
            'qscsextensions.qc': sha256(os.path.join(srcdir, 'qscsextensions.qc')),
            'qsmenuextensions.qc': sha256(os.path.join(srcdir, 'qsmenuextensions.qc')),
            'csprogs.dat': sha256(os.path.join(gamedir, 'csprogs.dat')),
            'menu.dat': sha256(os.path.join(gamedir, 'menu.dat')),
        },
    }
    with open(os.path.join(art, 'summary.json'), 'w') as f:
        json.dump(summary, f, indent=2)

    print("\n%d/%d passed  (logs and summary.json in %s)"
          % (summary['passed'], summary['total'], art))
    if not args.fteqcc:
        print("note: --fteqcc not given, so the progs were NOT rebuilt from the "
              "headers this binary just generated")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
