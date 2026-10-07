"""Stack-sample an engine's main thread while it loops a timedemo.

usage: profile_td.py --label L --exe path [--demo fps_aerowalk] [--delay 3] [--seconds 8] [--extra "cvar v;..."]
Writes profiles/<label>.txt (leaf modules, leaf functions, innermost engine caller, inclusive).
"""
import argparse, os, pickle, shutil, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'claude-prof'))
sys.path.insert(0, str(HERE.parent / 'engine-fps-20261005'))
import measure
from sampler import Sampler, report, symbolize

ap = argparse.ArgumentParser()
ap.add_argument('--label', required=True)
ap.add_argument('--exe', required=True)
ap.add_argument('--demo', default='fps_aerowalk')
ap.add_argument('--delay', type=float, default=3)
ap.add_argument('--seconds', type=float, default=8)
ap.add_argument('--extra', default='')
a = ap.parse_args()

exe = Path(a.exe).resolve()
base = HERE / 'runs' / ('prof-' + a.label)
base.mkdir(parents=True, exist_ok=True)
shutil.copy2(exe, base / exe.name)
pdb = exe.with_suffix('.pdb')
if pdb.exists():
    shutil.copy2(pdb, base / pdb.name)
for d in exe.parent.glob('*.dll'):
    shutil.copy2(d, base / d.name)
shutil.copytree(HERE.parent / 'engine-fps-20261005/assets/id1', base / 'id1', dirs_exist_ok=True)
shutil.copytree(HERE.parent / 'engine-fps-20261005/demos', base / 'id1', dirs_exist_ok=True)
shutil.copytree(HERE / 'assets', base, dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.json'))
cfg = measure.COMMON + measure.SPEC['qssm'] + a.extra.replace(';', '\n') + '\n'
cfg += ''.join(f'timedemo {a.demo}\n' + 'wait\n' * 30 for _ in range(200))
rc = 'exec default.cfg\nexec fpsbench.cfg\n'
(base / 'id1/quake.rc').write_text(rc); (base / 'id1/fpsbench.cfg').write_text(cfg)
measure.pak(base / 'id1/pak2.pak', {'quake.rc': rc, 'fpsbench.cfg': cfg})
args = [str(base / exe.name), '-basedir', str(base), '-nohome', '-condebug', '-window', '-width', '800', '-height', '600', '-nojoy', '-noice', '-listen', '-noudp', '-noquakeimport']
env = os.environ.copy(); env['APPDATA'] = str(base / 'profile'); env['LOCALAPPDATA'] = str(base / 'profile/local')
p = subprocess.Popen(args, cwd=base, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    time.sleep(a.delay)
    s = Sampler(p.pid, str(base))
    tid, util, raw, mods = s.run(a.seconds)
    st = symbolize(raw, mods)
finally:
    p.terminate(); p.wait(timeout=10)
(HERE / 'profiles').mkdir(exist_ok=True)
pickle.dump(st, open(HERE / 'profiles' / f'{a.label}.pkl', 'wb'))
(HERE / 'profiles' / f'{a.label}.txt').write_text(report(st))
print(report(st))
