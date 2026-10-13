"""Verify live weapon-wheel skin refreshes with HUD replacements disabled.

Usage: python3 Misc/stress/test_live_texture_wheel.py [binary] [id1-assets]
Requires SDL offscreen OpenGL and original pak0/pak1 files.
"""
from pathlib import Path
import argparse
import json
import os
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.codex-build/live-textures'
WORK.mkdir(parents=True, exist_ok=True)
(WORK / 'tmp').mkdir(exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', nargs='?', type=Path, default=ROOT / 'Quake/quakespasm')
parser.add_argument('assets', nargs='?', type=Path, default=ROOT / '.codex-build/beachvolley-md3-verification/quake/id1')
parser.add_argument('--legacy', action='store_true', help='disable VBOs and parallel texture preparation')
args = parser.parse_args()
run = Path(tempfile.mkdtemp(prefix='wheel-', dir=WORK))
game = run / 'id1'
game.mkdir()
for name in ('pak0.pak', 'pak1.pak'):
    (game / name).symlink_to((args.assets / name).resolve())
(game / 'progs').mkdir()
(game / 'progs/v_axe.mdl_0.tga').write_bytes(
    struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 128, 128, 32, 8)
    + bytes((20, 230, 90, 255)) * (128 * 128))

wait = 'wait\n' * 12
config = ('cl_demoreel 0\ncl_web_download_url ""\ncl_web_download_url2 ""\n'
          'host_maxfps 144\nvid_vsync 0\ndeveloper 1\ncl_pong 0\n'
          'gl_load24bit 0\ngl_load24bit_hud 0\nr_replacemodels ""\n')
if args.legacy:
    config += 'tex_parallel 0\nr_scenecache 0\n'
commands = config + 'map start\n' + wait + 'cmd pext\n' + wait * 8
commands += 'echo WHEEL_ORIGINAL_BEGIN\ntex_verify 1\n+weaponwheel\n' + wait
commands += 'echo WHEEL_REPLACEMENT_BEGIN\ngl_load24bit 1\n' + wait
commands += 'echo WHEEL_RESTORED_BEGIN\ngl_load24bit 0\n' + wait
commands += 'echo WHEEL_FINISHED\nimagelist\n-weaponwheel\nquit\n'
(game / 'config.cfg').write_text(config)
(game / 'autoexec.cfg').write_text(commands)
logpath = run / 'engine.log'
with logpath.open('w') as log:
    result = subprocess.run(
        [str(args.binary.resolve()), '-basedir', str(run), '-nohome', '-window',
         '-width', '320', '-height', '240', '-nosound', '-nojoy', '-nomouse',
         '-nolan', '-noipx', '-noquakeimport', *(['-novbo'] if args.legacy else [])], cwd=run,
        env={**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'LIBGL_ALWAYS_SOFTWARE': '1',
             'TMPDIR': str(WORK / 'tmp'), 'XDG_CACHE_HOME': str(WORK / 'cache')},
        stdout=log, stderr=subprocess.STDOUT, timeout=60)
text = logpath.read_text(errors='replace')
assert result.returncode == 0 and 'WHEEL_FINISHED' in text, text[-4000:]
assert not re.search(r'TexMgr_FreeTexture: not found|invalid source|Host_Error|Sys_Error|Segmentation', text)


def image_hash(begin, end):
    chunk = re.split(begin + r'\s*\n', text, maxsplit=1)[1].split(end, 1)[0]
    hashes = re.findall(r'^texhash weaponwheel_axe 0 (\d+ \d+ [0-9a-f]+ [0-9a-f]+)$', chunk, re.M)
    assert len(hashes) == 1, (begin, hashes, logpath)
    return hashes[0]


original = image_hash('WHEEL_ORIGINAL_BEGIN', 'WHEEL_REPLACEMENT_BEGIN')
replacement = image_hash('WHEEL_REPLACEMENT_BEGIN', 'WHEEL_RESTORED_BEGIN')
restored = image_hash('WHEEL_RESTORED_BEGIN', 'WHEEL_FINISHED')
assert replacement != original, 'weapon wheel did not adopt the replacement skin'
assert restored == original, 'weapon wheel did not restore the original skin'
assert len(re.findall(r'^\s+\d+ x\s*\d+ weaponwheel_axe$', text, re.M)) == 1, 'weapon-wheel texture records leaked'
report = {'result': 'PASS', 'legacy': args.legacy, 'run': str(run), 'checks': ['open weapon wheel refreshes with HUD replacements disabled',
          'replacement pixels change', 'original pixels restored', 'one texture record']}
(WORK / ('report-wheel-legacy.json' if args.legacy else 'report-wheel.json')).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
