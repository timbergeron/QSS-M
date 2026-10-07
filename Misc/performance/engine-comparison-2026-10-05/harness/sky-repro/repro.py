"""Repro: skybox kept across maps + gl_load24bit change frees textures behind the sky's back.

Runs a build through: 24-bit map with skybox A -> gl_load24bit 0 -> same map -> skybox B,
dumping imagelist after each stage. Reports world textures that vanished or changed.
usage: repro.py <label> <exe>
"""
import os, re, shutil, struct, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
FPS = HERE.parent / 'engine-fps-20261005'
sys.path.insert(0, str(FPS)); import measure

label, exe = sys.argv[1], Path(sys.argv[2]).resolve()
base = HERE / 'runs' / label
if base.exists(): shutil.rmtree(base)
(base / 'id1/gfx/env').mkdir(parents=True)
shutil.copy2(exe, base / exe.name)
for d in exe.parent.glob('*.dll'): shutil.copy2(d, base / d.name)
for n in ('pak0.pak', 'pak1.pak'): os.link(FPS / 'assets/id1' / n, base / 'id1' / n)

def tga(path, rgb):
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 8, 8, 32, 8)
    path.write_bytes(hdr + bytes([rgb[2], rgb[1], rgb[0], 255]) * 64)
for sky, rgb in (('reproa', (255, 0, 0)), ('reprob', (0, 0, 255))):
    for face in ('rt', 'bk', 'lf', 'ft', 'up', 'dn'):
        tga(base / f'id1/gfx/env/{sky}{face}.tga', rgb)

W = 'wait\n' * 40
cfg = measure.COMMON + measure.SPEC['qssm'] + f'''gl_load24bit 1
gl_load24bit_hud 1
map e1m1
{W}sky reproa
{W}echo STAGE1
imagelist
gl_load24bit 0
map e1m1
{W}echo STAGE2
imagelist
sky reprob
{W}echo STAGE3
imagelist
echo REPRO_DONE
quit
'''
rc = 'exec default.cfg\nexec repro.cfg\n'
(base / 'id1/quake.rc').write_text(rc); (base / 'id1/repro.cfg').write_text(cfg)
measure.pak(base / 'id1/pak2.pak', {'quake.rc': rc, 'repro.cfg': cfg})
env = os.environ.copy(); env['APPDATA'] = str(base / 'profile'); env['LOCALAPPDATA'] = str(base / 'profile/local')
args = [str(base / exe.name), '-basedir', str(base), '-nohome', '-condebug', '-window', '-width', '800', '-height', '600', '-nojoy', '-noice', '-listen', '-noudp', '-noquakeimport']
p = subprocess.Popen(args, cwd=base, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
start = time.monotonic()
while time.monotonic() - start < 60 and p.poll() is None:
    time.sleep(0.3)
if p.poll() is None:
    p.terminate()
log = (base / 'qconsole.log').read_text(errors='replace')
(HERE / f'{label}.log').write_text(log)

def stage(n):
    s = log.split(f'STAGE{n}')[1].split('STAGE' if n < 3 else 'REPRO_DONE')[0]
    return [m.group(3) for m in re.finditer(r'^\s+(\d+) x\s*(\d+) (.+)$', s, re.M)]
s1, s2, s3 = stage(1), stage(2), stage(3)
world2 = [n for n in s2 if n.startswith('maps/e1m1.bsp:')]
world3 = [n for n in s3 if n.startswith('maps/e1m1.bsp:')]
sky3 = sorted(n for n in s3 if 'gfx/env/' in n)
print(label, '| stage1 textures', len(s1), '| stage2', len(s2), '| stage3', len(s3))
print('  world textures after reload:', len(world2), '| after second skybox:', len(world3))
print('  lost world textures:', sorted(set(world2) - set(world3))[:8])
print('  skybox faces at end:', sky3)
print('  duplicate names at end:', sorted({n for n in s3 if s3.count(n) > 1})[:8])
