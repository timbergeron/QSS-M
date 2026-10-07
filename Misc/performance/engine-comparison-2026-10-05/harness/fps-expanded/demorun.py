"""Fresh-process timedemos for the long-form benchmarks (CTF match, ad_tears speedrun).

Mirrors engine-fps-20261005/measure.run: same common/engine settings, one owned
process per pass, FPS parsed from the native timedemo line, physical 800x600
client area checked. Differences: a game directory per benchmark, a longer
timeout, configs packed as the next consecutive pak (pak2 in id1, pak3 in ad,
since AD ships pak0-2 and classic engines stop at the first gap), the engine
window brought to the foreground, 60 FPS focus-throttled passes rejected and
retried, and data hard-linked instead of copied.

usage: demorun.py --bench ctf|adtears [--engines qssm,ironwail,...] [--qssm-exe path] [--passes 5] [--out results-name]
"""
import argparse, hashlib, json, os, re, shutil, statistics, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
FPSROOT = HERE.parent / 'engine-fps-20261005'
sys.path.insert(0, str(FPSROOT))
import measure

BENCH = {
    'ctf': {'game': 'id1', 'demo': 'ctfmatch', 'title': 'CTF match · ctf3m2 · 16 players',
            'files': {'id1/ctfmatch.dem': HERE / 'assets/id1/ctfmatch.dem', 'id1/pak3.pak': HERE / 'assets/id1/pak3.pak',
                      'id1/maps/ctf3m2.bsp': FPSROOT / 'assets/id1/maps/ctf3m2.bsp'},
            'engines': ['qssm', 'ironwail', 'vkquake', 'qss', 'fteqw', 'ezquake'],
            'qssm_extra': 'scr_autoid 0\n'},
    'adtears': {'game': 'ad', 'demo': 'adte_214', 'title': 'ad_tears · Sphere easy run 2:14',
                'files': {'ad/adte_214.dem': HERE / 'ad-data/adte_214.dem', 'ad/pak0.pak': HERE / 'ad-data/ad/pak0.pak',
                          'ad/pak1.pak': HERE / 'ad-data/ad/pak1.pak', 'ad/pak2.pak': HERE / 'ad-data/ad/pak2.pak'},
                'engines': ['qssm', 'ironwail', 'vkquake', 'qss', 'fteqw'],
                'qssm_extra': 'scr_autoid 0\n'},
}

ap = argparse.ArgumentParser()
ap.add_argument('--bench', required=True, choices=sorted(BENCH))
ap.add_argument('--engines')
ap.add_argument('--qssm-exe')
ap.add_argument('--passes', type=int, default=5)
ap.add_argument('--out', default=None)
ap.add_argument('--timeout', type=float, default=300)
ap.add_argument('--qssm-extra', default='', help='extra QSS-M cvars, e.g. "gl_powerupshells 0;cl_damagehue 0"')
a = ap.parse_args()
B = BENCH[a.bench]
if a.qssm_exe:
    measure.SOURCES['qssm'] = Path(a.qssm_exe).resolve()
keys = a.engines.split(',') if a.engines else B['engines']
ROOT = HERE / 'demoruns' / a.bench


def link(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        return
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def prepare(key):
    base = ROOT / key
    base.mkdir(parents=True, exist_ok=True)
    src = measure.SOURCES[key]
    if key in ('qssm', 'fteqw'):
        shutil.copy2(src, base / src.name)
        for d in src.parent.glob('*.dll'):
            shutil.copy2(d, base / d.name)
    else:
        shutil.copytree(src.parent, base, dirs_exist_ok=True)
    for n in ('pak0.pak', 'pak1.pak'):
        link(FPSROOT / 'assets/id1' / n, base / 'id1' / n)
    for rel, srcf in B['files'].items():
        link(srcf, base / rel)
    for g in ('id1', 'qw', B['game']):
        (base / g).mkdir(exist_ok=True)
    return base


from focusutil import focus


def run(key, index, warmup):
    base = ROOT / key
    exe = base / measure.SOURCES[key].name
    game = B['game']
    for f in base.rglob('*.log'):
        f.unlink(missing_ok=True)
    for f in base.rglob('config.cfg'):
        f.unlink(missing_ok=True)
    marker = f'FPS_BENCH_{key}_{a.bench}_{time.time_ns()}'
    controls = measure.COMMON + measure.SPEC[key] + (B['qssm_extra'] + ''.join(c.strip() + chr(10) for c in a.qssm_extra.split(';') if c.strip()) if key == 'qssm' else '')
    if key in ('ironwail', 'vkquake'):
        # Same unfocused-sleep opt-out the harness already gives QSS-M and QSS; demos
        # have no listen server, so -listen alone does not stop the 16 ms sleep.
        controls += 'sys_throttle -1\n'
    cfg = controls + f'echo {marker}\nvid_width\nvid_height\nvid_vsync\ntimedemo {B["demo"]}\n'
    rc = 'exec default.cfg\nexec fpsbench.cfg\n' if key != 'fteqw' else 'exec default.cfg\nexec config.cfg\nstuffcmds\n'
    for g in {'id1', 'qw', game}:
        (base / g / 'fpsbench.cfg').write_text(cfg)
        (base / g / 'config.cfg').write_text(controls)
        (base / g / 'autoexec.cfg').write_text('exec fpsbench.cfg\n' if key == 'ezquake' else '')
        # Classic engines stop at the first missing pakN, so use the next free number.
        measure.pak(base / g / ('pak3.pak' if g == 'ad' else 'pak2.pak'), {'quake.rc': rc, 'fpsbench.cfg': cfg, 'config.cfg': controls})
    args = [str(exe), '-basedir', str(base), '-nohome', '-condebug', '-window', '-width', '800', '-height', '600', '-nojoy', '-noice']
    if game != 'id1':
        args += ['-game', game]
    if key in ('qssm', 'qss', 'ironwail', 'vkquake'):
        args += ['-listen', '-noudp']
    if key == 'qssm':
        args += ['-noquakeimport']
    if key == 'fteqw':
        args += ['-noupdates', '+exec', 'fpsbench.cfg']
    if key == 'ezquake':
        args += ['+cl_verify_qwprotocol', '0', '+set', 'vid_win_width', '800', '+set', 'vid_win_height', '600', '+set', 'vid_width', '800', '+set', 'vid_height', '600']
    env = os.environ.copy(); env['APPDATA'] = str(base / 'profile'); env['LOCALAPPDATA'] = str(base / 'profile/local')
    (base / 'profile/local').mkdir(parents=True, exist_ok=True)
    start = time.monotonic(); result = None; geometry = None; txt = ''
    with (base / 'stdout.log').open('wb') as out:
        proc = subprocess.Popen(args, cwd=base, stdout=out, stderr=subprocess.STDOUT, env=env)
        try:
            focused = False
            while time.monotonic() - start < a.timeout:
                geometry = measure.window_pixels(proc.pid) or geometry
                if geometry and not focused and time.monotonic() - start < 20:
                    focused = focus(proc.pid)
                logs = list(base.rglob('*.log'))
                if key == 'vkquake':
                    logs.append(Path(os.environ['APPDATA']) / 'vkQuake/qconsole.log')
                for p in logs:
                    try:
                        t = p.read_text(errors='replace')
                    except (PermissionError, FileNotFoundError):
                        continue
                    if marker not in t:
                        continue
                    t = t[t.rfind(marker):]
                    m = [x for x in measure.FPS.finditer(t) if int(x[1]) > 1000 and float(x[3]) > 0]
                    if m or not txt:
                        txt = t
                    if m:
                        result = {'frames': int(m[-1][1]), 'seconds': float(m[-1][2]), 'fps': float(m[-1][3]), 'line': m[-1][0]}
                        break
                if result:
                    break
                if proc.poll() is not None:
                    time.sleep(0.3)  # read once more: some engines flush the log at exit
                    for p in list(base.rglob('*.log')):
                        t = p.read_text(errors='replace')
                        if marker not in t:
                            continue
                        t = t[t.rfind(marker):]
                        m = [x for x in measure.FPS.finditer(t) if int(x[1]) > 1000 and float(x[3]) > 0]
                        if m:
                            txt = t
                            result = {'frames': int(m[-1][1]), 'seconds': float(m[-1][2]), 'fps': float(m[-1][3]), 'line': m[-1][0]}
                            break
                    break
                time.sleep(0.1)
        finally:
            if proc.poll() is None:
                proc.terminate(); proc.wait(timeout=10)
    label = f'{key}-{a.bench}-' + ('warmup' if warmup else f'{index:02}')
    (HERE / 'demoruns/logs').mkdir(parents=True, exist_ok=True)
    (HERE / 'demoruns/logs' / (label + '.log')).write_text(txt)
    if result and geometry != {'width': 800, 'height': 600}:
        raise RuntimeError(f'unexpected client size {key} {geometry}')
    print(label, result['line'] if result else 'FAILED', flush=True)
    return {'engine': key, 'bench': a.bench, 'run': index, 'warmup': warmup, 'result': result, 'window_pixels': geometry,
            'elapsed_wall_seconds': time.monotonic() - start, 'foreground': focused, 'command': args, 'config': cfg, 'log': 'demoruns/logs/' + label + '.log'}


for k in keys:
    prepare(k)
out = HERE / (a.out or f'{a.bench}-results.json')
raw = json.loads(out.read_text()) if out.exists() else {
    'bench': a.bench, 'title': B['title'], 'game': B['game'], 'demo': B['demo'],
    'method': 'One discarded warm-up then five fresh-process native timedemos per engine, rotating engine order; 800x600 window, VSync/MSAA off, uncapped, sound on; same settings as the nine-map FPS suite. QSS-M runs with scr_autoid 0.',
    'files': {rel: hashlib.sha256(Path(p).read_bytes()).hexdigest() for rel, p in B['files'].items() if Path(p).stat().st_size < 200_000_000},
    'engines': {k: {'exe': str(measure.SOURCES[k]), 'sha256': hashlib.sha256(measure.SOURCES[k].read_bytes()).hexdigest()} for k in keys},
    'samples': []}
done = {(s['engine'], s['run'], s['warmup']) for s in raw['samples'] if s['result']}
for idx in range(a.passes + 1):
    order = keys[idx % len(keys):] + keys[:idx % len(keys)]
    for k in order:
        if (k, idx, idx == 0) in done:
            continue
        for attempt in range(3):
            row = run(k, idx, idx == 0)
            # A Quakespasm-family window without focus sleeps 16 ms per frame (~60 FPS).
            if row['result'] and row['result']['fps'] < 120:
                row['exclusion_reason'] = 'Focus throttle: ran at ~60 FPS; retried.'
                raw.setdefault('excluded_samples', []).append(row)
                out.write_text(json.dumps(raw, indent=2))
                continue
            break
        raw['samples'].append(row)
        out.write_text(json.dumps(raw, indent=2))
for k in keys:
    v = [s['result']['fps'] for s in raw['samples'] if s['engine'] == k and not s['warmup'] and s['result']]
    if v:
        print(k, 'median', round(statistics.median(v), 1), v)
