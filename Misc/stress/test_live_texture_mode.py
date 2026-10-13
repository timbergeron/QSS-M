"""Run live gl_load24bit transitions using the real renderer and fixture textures.

Usage: python3 Misc/stress/test_live_texture_mode.py [binary] [id1-assets]
Requires SDL's offscreen driver, software OpenGL, and original pak0/pak1 files.
Artifacts (logs, screenshots and saves) stay in .codex-build/live-textures.
"""
from pathlib import Path
import argparse
import collections
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.codex-build/live-textures'
WORK.mkdir(parents=True, exist_ok=True)
(WORK / 'tmp').mkdir(exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', nargs='?', type=Path, default=ROOT / 'Quake/quakespasm')
parser.add_argument('assets', nargs='?', type=Path, default=ROOT / '.codex-build/beachvolley-md3-verification/quake/id1')
parser.add_argument('--legacy', action='store_true', help='disable VBOs and parallel texture preparation')
parser.add_argument('--sprite-groups', action='store_true', help='exercise indexed/RGBA sprite groups and angled frames')
args = parser.parse_args()
BINARY, ASSETS = args.binary.resolve(), args.assets.resolve()
run = Path(tempfile.mkdtemp(prefix='run-', dir=WORK))
game = run / 'id1'
game.mkdir()


def entries(pak):
    data = pak.read_bytes()
    magic, offset, length = struct.unpack_from('<4sII', data)
    assert magic == b'PACK'
    for i in range(offset, offset + length, 64):
        name, start, size = struct.unpack_from('<56sII', data, i)
        yield name.split(b'\0')[0].decode(), data[start:start + size]


files = {}
for n in ('pak0.pak', 'pak1.pak'):
    pak = ASSETS / n
    (game / n).symlink_to(pak)
    files.update(entries(pak))


def tga(name, width=64, height=64, color=(20, 180, 240)):
    path = game / (name + '.tga')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 8)
                     + bytes((*color[::-1], 255)) * (width * height))


# Every ordinary texture gets an unmistakable replacement. Mode 2 must still
# use the originals for world surfaces, while replacing liquids/item skins.
for name in ('maps/start.bsp', 'maps/e1m1.bsp', 'maps/b_bh10.bsp', 'maps/b_bh25.bsp'):
    data = files[name]
    offset, length = struct.unpack_from('<II', data, 4 + 2 * 8)
    count, = struct.unpack_from('<I', data, offset)
    for i in range(count):
        relative, = struct.unpack_from('<i', data, offset + 4 + i * 4)
        if relative < 0:
            continue
        texture = data[offset + relative:offset + relative + 16].split(b'\0')[0].decode()
        safe = '#' + texture[1:] if texture.startswith('*') else texture
        base = 'textures/' + Path(name).stem + '/' + safe
        if texture.lower().startswith('sky'):
            tga(base + '_back', color=(200, 40, 60))
            tga(base + '_front', color=(40, 200, 60))
        else:
            tga(base)
            tga(base + '_glow', color=(80, 30, 10))
for face in ('rt', 'bk', 'lf', 'ft', 'up', 'dn'):
    tga('gfx/env/livecheck' + face, color=(40, 100, 200))
tga('gfx/conchars', 256, 256)
tga('gfx/conback', 64, 64)
tga('progs/v_shot.mdl_0', 128, 128)
tga('progs/v_shot.mdl_0_glow', 128, 128, color=(80, 30, 10))
tga('progs/player.mdl_0', 128, 128)
# A required MD3 skin has no indexed fallback; it must stay valid in all
# texture modes and through cached-model rebuilds.
frame = struct.pack('<10f16s', -1, -1, -1, 1, 1, 1, 0, 0, 0, 2, b'fixture')
triangles = struct.pack('<3i', 0, 1, 2)
shader = struct.pack('<64si', b'progs/review_md3', 0)
texcoords = struct.pack('<6f', 0, 0, 1, 0, 0, 1)
vertices = b''.join(struct.pack('<3h2B', x, y, 0, 0, 0) for x, y in [(0, 0), (64, 0), (0, 64)])
surface = struct.pack('<4s64s10i', b'IDP3', b'fixture', 0, 1, 1, 3, 1,
                      108, 120, 188, 212, 236) + triangles + shader + texcoords + vertices
md3 = struct.pack('<4si64s9i', b'IDP3', 15, b'fixture', 0, 1, 0, 1, 0,
                  108, 164, 164, 400) + frame + surface
(game / 'progs/v_axe.md3').write_bytes(md3)
tga('progs/review_md3', 32, 32)
# Unequal replacement dimensions also exercise the sprite's padded UV update.
tga('progs/s_explod.spr_0', 128, 64)
if args.sprite_groups:
    def sprite(version, types):
        data = struct.pack('<4sii f iii f i', b'IDSP', version, 2, 24, 16, 16, len(types), 0, 0)
        for index, kind in enumerate(types):
            count = 1 if kind == 0 else 8 if kind == 2 else 3
            data += struct.pack('<i', kind)
            if kind:
                data += struct.pack('<i', count) + struct.pack('<' + 'f' * count, *([0.1] * count))
            for subframe in range(count):
                pixels = bytes((20 + index + subframe,)) if version == 1 else bytes((30, 90 + index, 160 + subframe, 255))
                data += struct.pack('<4i', -8, 8, 16, 16) + pixels * 256
        return data

    (game / 'progs/s_bubble.spr').write_bytes(sprite(1, [0, 1]))
    (game / 'progs/s_explod.spr').write_bytes(sprite(32, [0, 1, 2, 0, 0, 0]))
    # Non-power-of-two replacements also exercise padded UVs on older GL.
    tga('progs/s_bubble.spr_101', 30, 18)
    tga('progs/s_explod.spr_101', 30, 18)
    tga('progs/s_explod.spr_205', 20, 30)

wait = 'wait\n' * 12
config = ('cl_demoreel 0\ncl_web_download_url ""\ncl_web_download_url2 ""\n'
          'host_maxfps 144\nvid_vsync 0\ndeveloper 1\ngl_load24bit 0\n'
          'gl_load24bit_hud 1\ngl_load24bit_skins 1\nr_fastsky 0\n'
          'r_replacemodels "md3"\nr_scenecache 1\ncl_pong 0\n')
if args.legacy:
    config += 'tex_parallel 0\nr_scenecache 0\n'
if args.sprite_groups:
    config += 'tex_verify 1\n'
commands = config + 'map start\n' + wait + 'cmd pext\n' + wait * 8 + 'toggleconsole\npause\n' + wait * 4
commands += 'sky livecheck\n' + wait
modes = [0, 1, 2, 0, 1, 0, 2, 1, 0]
for i, mode in enumerate(modes):
    commands += f'gl_load24bit {mode}\n' + wait
    commands += f'echo LIVE_STAGE_{i}\nimagelist\nsave live{i}\nscreenshot png\nhunk_print\n' + wait
# Repeated cycles catch leaked GL records and hunk-backed classic sky layers.
for i in range(12):
    commands += 'gl_load24bit 1\n' + wait + 'gl_load24bit 0\n' + wait
commands += 'echo LIVE_STRESS\nimagelist\nhunk_print\n' + wait
commands += 'echo LIVE_COALESCED_BEGIN\ntex_verify 1\ngl_load24bit 1\ngl_load24bit 0\n' + wait
commands += 'echo LIVE_COALESCED_END\ntex_verify 0\n' + wait
commands += 'gl_load24bit 1\n' + wait + 'gl_load24bit_hud 0\n' + wait
commands += 'echo LIVE_HUD_OFF\nimagelist\n' + wait
commands += 'gl_load24bit_hud 1\n' + wait + 'echo LIVE_HUD_ON\nimagelist\n' + wait
commands += 'echo LIVE_RESTART_BEGIN\ntex_verify 1\nvid_unlock\nvid_width 336\nvid_restart\n' + wait * 2 + 'echo LIVE_RESTART\nimagelist\ntex_verify 0\n' + wait
commands += 'gl_load24bit 0\n' + wait + 'echo LIVE_RESTART_ORIGINAL\nimagelist\n' + wait
commands += 'echo LIVE_FRESH_MAP_BEGIN\ngl_load24bit_hud 0\ntex_verify 1\ngl_load24bit 1\nmap e1m1\n' + wait + 'cmd pext\n' + wait * 8 + 'echo LIVE_FRESH_MAP_END\nimagelist\ntex_verify 0\n' + wait + 'gl_load24bit 0\n' + wait
commands += 'gl_load24bit 2\n' + wait + 'gl_load24bit 1\n' + wait
commands += 'echo LIVE_MAPCHANGE\nimagelist\n' + wait
# Retained item textures must use the mode in which they were actually made.
# In mode 2 their diffuse replacements exist, but their glow images do not.
commands += 'gl_load24bit 2\n' + wait + 'echo LIVE_RETAIN_MODE2\nimagelist\n' + wait
commands += 'gl_load24bit 1\nmap e1m1\n' + wait + 'cmd pext\n' + wait * 8
commands += 'echo LIVE_RETAIN_MODE1\nimagelist\n' + wait
commands += 'echo LIVE_EQUIVALENT_BEGIN\ntex_verify 1\ngl_load24bit 3\n' + wait
commands += 'echo LIVE_EQUIVALENT_END\ntex_verify 0\n' + wait
# Disconnect keeps cached models alive. Refreshes must still leave them usable
# when a saved map is loaded, including a mode change in the same command batch.
commands += 'disconnect\n' + wait + 'gl_load24bit 0\n' + wait
commands += 'gl_load24bit_hud 1\nmenu_main\n' + wait + 'echo LIVE_DISCONNECTED\nimagelist\n' + wait
commands += 'gl_load24bit 1\nload live0\n' + wait + 'cmd pext\n' + wait * 8
commands += 'echo LIVE_SAVELOAD\nimagelist\n' + wait + 'quit\n'
(game / 'config.cfg').write_text(config)
(game / 'autoexec.cfg').write_text(commands)
logpath = run / 'engine.log'
env = {**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'LIBGL_ALWAYS_SOFTWARE': '1',
       'TMPDIR': str(WORK / 'tmp'), 'XDG_CACHE_HOME': str(WORK / 'cache')}
print(f'Running live texture checks: {run}', flush=True)
with logpath.open('w') as log:
    result = subprocess.run([str(BINARY), '-basedir', str(run), '-nohome', '-window',
                             '-width', '320', '-height', '240', '-nosound', '-nojoy',
                             '-nomouse', '-nolan', '-noipx', '-noquakeimport', *(['-novbo'] if args.legacy else []),
                             *(['-notexturenpot'] if args.sprite_groups else [])],
                            cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
text = logpath.read_text(errors='replace')
assert result.returncode == 0, text[-4000:]
assert 'LIVE_MAPCHANGE' in text, text[-4000:]
restart = re.split(r'LIVE_RESTART_BEGIN\s*\n', text, maxsplit=1)[1].split('LIVE_RESTART ', 1)[0]
assert len(re.findall(r'^texhash (?:maps/|textures/|progs/)', restart, re.M)) > 100, 'vid_restart did not reload model/world images'
assert not re.search(r'TexMgr_FreeTexture: not found|invalid source|Host_Error|Sys_Error|Segmentation', text), text[-4000:]


def gameplay_save(index):
    saved = (game / 'saves' / f'live{index}.sav').read_text()
    # The engine publishes the current host frame duration even while paused.
    return re.sub(r'^"frametime" "[^"]*"$', '', saved, flags=re.M)


def stage(label):
    chunk = re.split(re.escape(label) + r'\s*\n', text, maxsplit=1)[1].split('LIVE_', 1)[0]
    return [(int(w), int(h), name.strip()) for w, h, name in
            re.findall(r'^\s+(\d+) x\s*(\d+) (.+)$', chunk, re.M)]


for i, mode in enumerate(modes):
    images = stage(f'LIVE_STAGE_{i}')
    names = [name for _, _, name in images]
    assert len(names) == len(set(names)), collections.Counter(names)
    assert len([name for name in names if 'gfx/env/livecheck' in name]) == 6
    external_world = [name for name in names if name.startswith('textures/start/') and 'sky' not in name and '/#' not in name]
    assert bool(external_world) == (mode == 1), (mode, external_world)
    assert bool([name for name in names if name.startswith('textures/start/#')]) == (mode != 0)
    assert bool([name for name in names if name.startswith('textures/start/sky')]) == (mode != 0)
    assert bool([name for name in names if name.startswith('textures/start/') and name.endswith('_glow')]) == (mode == 1)
    assert bool([name for name in names if name == 'progs/v_shot.mdl_0']) == (mode != 0)
    assert bool([name for name in names if name == 'progs/v_shot.mdl_0_glow']) == (mode == 1)
    assert bool([name for name in names if name == 'progs/player.mdl_0']) == (mode != 0)
    assert (32, 32, 'progs/review_md3') in images, 'required MD3 skin lost during reload'
    assert bool([name for name in names if name == 'progs/s_explod.spr_0']) == (mode != 0)
    if args.sprite_groups:
        for name in ('progs/s_bubble.spr_101', 'progs/s_explod.spr_101', 'progs/s_explod.spr_205'):
            assert (name in names) == (mode != 0), (mode, name)
        assert len([name for name in names if name.startswith('progs/s_bubble.spr')]) == 4
        assert len([name for name in names if name.startswith('progs/s_explod.spr')]) == 15
    font = next((w, h) for w, h, name in images if name == 'gfx.wad:conchars')
    assert font == ((256, 256) if mode else (128, 128)), (mode, font)
    # Local pause makes game time and all entities directly comparable.
    assert gameplay_save(i) == gameplay_save(0), f'game state changed at stage {i}'
assert sorted(stage('LIVE_STRESS')) == sorted(stage('LIVE_STAGE_0'))
if args.sprite_groups:
    def sprite_hashes(chunk):
        return sorted(re.findall(r'^texhash (progs/s_(?:bubble|explod)\.spr\S*) (\d+ \d+ \d+ [0-9a-f]+ [0-9a-f]+)$', chunk, re.M))

    original_sprite_hashes = sprite_hashes(text.split('LIVE_STAGE_0', 1)[0])
    assert len(original_sprite_hashes) == 19, 'grouped sprite originals did not upload'
    for index in (3, 5, 8):
        chunk = text.split(f'LIVE_STAGE_{index - 1}', 1)[1].split(f'LIVE_STAGE_{index}', 1)[0]
        assert sprite_hashes(chunk) == original_sprite_hashes, 'grouped sprite original pixels changed during refresh'
used = re.findall(r'^\s*\d+ :\s*(\d+) : USED$', text, re.M)
assert len(used) == len(modes) + 1 and len(set(used)) == 1, ('hunk grew during live switches', used)
for label, expected in [('LIVE_HUD_OFF', (128, 128)), ('LIVE_HUD_ON', (256, 256)), ('LIVE_RESTART', (256, 256))]:
    assert next((w, h) for w, h, name in stage(label) if name == 'gfx.wad:conchars') == expected
assert sorted(stage('LIVE_RESTART_ORIGINAL')) == sorted(stage('LIVE_STAGE_0')), 'original images changed after vid_restart'
coalesced = re.split(r'LIVE_COALESCED_BEGIN\s*\n', text, maxsplit=1)[1].split('LIVE_COALESCED_END', 1)[0]
assert not re.search(r'^texhash (?:maps/|textures/|progs/)', coalesced, re.M), 'same final mode reloaded images unnecessarily'
fresh_map = re.split(r'LIVE_FRESH_MAP_BEGIN\s*\n', text, maxsplit=1)[1].split('LIVE_FRESH_MAP_END', 1)[0]
fresh_images = re.findall(r'^texhash (textures/e1m1/\S+) (\d+) .*$', fresh_map, re.M)
assert len(fresh_images) > 100 and len(fresh_images) == len(set(fresh_images)), 'fresh brush textures reloaded while cached alias skins changed'
assert (128, 128, 'progs/v_shot.mdl_0') in stage('LIVE_FRESH_MAP_END'), 'cached alias skin did not adopt the new map mode'
item_glows = lambda images: sorted((w, h, name) for w, h, name in images
                                    if name.startswith(('textures/b_bh10/', 'textures/b_bh25/')) and name.endswith('_glow'))
assert not item_glows(stage('LIVE_RETAIN_MODE2')), 'mode 2 retained external item glow images'
expected_item_glows = item_glows(stage('LIVE_MAPCHANGE'))
assert expected_item_glows, 'item glow fixture did not load'
assert item_glows(stage('LIVE_RETAIN_MODE1')) == expected_item_glows, 'retained item textures lost their mode 1 glow images across a map change'
equivalent = re.split(r'LIVE_EQUIVALENT_BEGIN\s*\n', text, maxsplit=1)[1].split('LIVE_EQUIVALENT_END', 1)[0]
assert not re.search(r'^texhash (?:maps/|textures/|progs/)', equivalent, re.M), 'equivalent numeric mode unnecessarily reloaded model images'
assert next((w, h) for w, h, name in stage('LIVE_DISCONNECTED') if name == 'gfx.wad:conchars') == (128, 128)
assert (128, 128, 'progs/v_shot.mdl_0') in stage('LIVE_SAVELOAD'), 'save load kept stale alias skins'
assert any(name.startswith('textures/start/') and 'sky' not in name and '/#' not in name
           for _, _, name in stage('LIVE_SAVELOAD')), 'save load did not adopt the new world mode'
# Only the explicit map commands may spawn a server.
assert len(re.findall(r'SV_SpawnServer total:', text)) == 4
report = {'result': 'PASS', 'legacy': args.legacy, 'sprite_groups': args.sprite_groups, 'run': str(run), 'transitions': len(modes) + 24,
          'checks': ['world modes 0/1/2', 'liquids/skies', 'alias/player skins', 'MD3 model/skin', 'sprite replacement',
                     'unchanged saved gameplay state', 'skybox preserved', 'no duplicate/leaked texture records',
                     'HUD toggle', 'coalesced changes skip redundant uploads', 'video restart', 'map change', 'fresh maps skip brush reload while cached skins update',
                     'retained item textures adopt new mode across a map change', 'equivalent numeric modes', 'disconnect/menu refresh', 'save load with pending texture changes'],
          'screenshots': [str(p) for p in sorted((game / 'screenshots').glob('*.png'))]}
if args.sprite_groups:
    report['checks'].extend(['indexed/RGBA grouped sprites', 'angled sprite frames', 'non-power-of-two padding', 'original sprite mip hashes preserved'])
report_name = 'report' + ('-groups' if args.sprite_groups else '') + ('-legacy' if args.legacy else '') + '.json'
(WORK / report_name).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
