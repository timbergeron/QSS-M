"""Play one demo N times in a single engine process; report each pass's FPS and any TDPROBE lines.

usage: repeat.py --label L --exe path/to/engine.exe [--engine qssm] [--demo fps_aerowalk] [--passes 8]
                 [--extra "cvar value;cvar value"] [--width 800 --height 600] [--game id1]
Uses the FPS harness's assets, demos, common settings and engine-specific settings.
"""
import argparse, json, os, re, shutil, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
FPSROOT = HERE.parent / 'engine-fps-20261005'
sys.path.insert(0, str(FPSROOT))
import measure

ap = argparse.ArgumentParser()
ap.add_argument('--label', required=True)
ap.add_argument('--exe', required=True)
ap.add_argument('--engine', default='qssm')
ap.add_argument('--demo', default='fps_aerowalk')
ap.add_argument('--passes', type=int, default=8)
ap.add_argument('--extra', default='')
ap.add_argument('--width', type=int, default=800)
ap.add_argument('--height', type=int, default=600)
ap.add_argument('--game', default='id1')
ap.add_argument('--timeout', type=float, default=180)
ap.add_argument('--pre-waits', type=int, default=0)
a = ap.parse_args()

exe = Path(a.exe).resolve()
base = HERE / 'runs' / a.label
base.mkdir(parents=True, exist_ok=True)
if a.engine in ('qssm', 'fteqw'):
    shutil.copy2(exe, base / exe.name)
    for d in exe.parent.glob('*.dll'):
        shutil.copy2(d, base / d.name)
else:
    shutil.copytree(exe.parent, base, dirs_exist_ok=True)
shutil.copytree(FPSROOT / 'assets/id1', base / 'id1', dirs_exist_ok=True)
shutil.copytree(FPSROOT / 'demos', base / 'id1', dirs_exist_ok=True)
extra_assets = HERE / 'assets'
if extra_assets.exists():
    shutil.copytree(extra_assets, base, dirs_exist_ok=True)
for p in base.rglob('*.log'):
    p.unlink()
for p in base.rglob('config.cfg'):
    p.unlink()

game = a.game
spec = measure.SPEC[a.engine]
common = measure.COMMON.replace('vid_width 800', f'vid_width {a.width}').replace('vid_height 600', f'vid_height {a.height}')
cfg = common + spec + a.extra.replace(';', '\n') + '\n' + 'wait\n' * a.pre_waits + 'echo REPEAT_BEGIN\n'
for i in range(a.passes):
    cfg += f'echo PASS_{i}\ntimedemo {a.demo}\n' + 'wait\n' * 120
cfg += 'echo REPEAT_DONE\n'
rc = 'exec default.cfg\nexec fpsbench.cfg\n' if a.engine != 'fteqw' else 'exec default.cfg\nexec config.cfg\nstuffcmds\n'
for g in {'id1', game}:
    (base / g).mkdir(exist_ok=True)
    (base / g / 'quake.rc').write_text(rc)
    (base / g / 'config.cfg').write_text(common + spec)
    (base / g / 'fpsbench.cfg').write_text(cfg)
    (base / g / 'autoexec.cfg').write_text('exec fpsbench.cfg\n' if a.engine == 'ezquake' else '')
    measure.pak(base / g / 'pak2.pak', {'quake.rc': rc, 'fpsbench.cfg': cfg, 'config.cfg': common + spec})

args = [str(base / exe.name), '-basedir', str(base), '-nohome', '-condebug', '-window', '-width', str(a.width), '-height', str(a.height), '-nojoy', '-noice']
if game != 'id1':
    args += ['-game', game]
if a.engine in ('qssm', 'qss', 'ironwail', 'vkquake'):
    args += ['-listen', '-noudp']
if a.engine == 'qssm':
    args += ['-noquakeimport']
if a.engine == 'fteqw':
    args += ['-noupdates', '+exec', 'fpsbench.cfg']
env = os.environ.copy(); env['APPDATA'] = str(base / 'profile'); env['LOCALAPPDATA'] = str(base / 'profile/local')
(base / 'profile/local').mkdir(parents=True, exist_ok=True)
txt = ''
with (base / 'stdout.log').open('wb') as out:
    p = subprocess.Popen(args, cwd=base, env=env, stdout=out, stderr=subprocess.STDOUT)
    start = time.monotonic(); geometry = None
    try:
        while time.monotonic() - start < a.timeout:
            geometry = measure.window_pixels(p.pid) or geometry
            logs = list(base.rglob('qconsole.log'))
            if a.engine == 'vkquake':
                logs.append(Path(env['APPDATA']) / 'vkQuake/qconsole.log')
            for log in logs:
                if log.exists():
                    t = log.read_text(errors='replace')
                    if 'REPEAT_BEGIN' in t:
                        txt = t
            if len([m for m in measure.FPS.finditer(txt)]) >= a.passes or 'REPEAT_DONE' in txt:
                break
            if p.poll() is not None:
                break
            time.sleep(0.1)
    finally:
        if p.poll() is None:
            p.terminate(); p.wait(timeout=10)
fps = [float(m[3]) for m in measure.FPS.finditer(txt)]
frames = [int(m[1]) for m in measure.FPS.finditer(txt)]
probes = [l for l in txt.splitlines() if l.startswith('TDPROBE frames')]
(HERE / 'logs').mkdir(exist_ok=True)
(HERE / 'logs' / f'{a.label}.log').write_text(txt)
row = {'label': a.label, 'engine': a.engine, 'exe': str(exe), 'demo': a.demo, 'extra': a.extra, 'geometry': geometry, 'fps': fps, 'frames': frames, 'probes': probes, 'command': args}
res = HERE / 'repeat-results.json'
rows = json.loads(res.read_text()) if res.exists() else []
rows = [r for r in rows if r['label'] != a.label] + [row]
res.write_text(json.dumps(rows, indent=2))
print(a.label, 'fps', fps)
for l in probes:
    print(' ', l[:260])
