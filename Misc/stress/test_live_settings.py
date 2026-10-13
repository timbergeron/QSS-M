"""Exercise live graphics/lighting/audio settings on a real paused map.

python3 Misc/stress/test_live_settings.py Quake/quakespasm --paks /path/to/id1
Uses offscreen Mesa and SDL dummy audio. Artifacts stay in build/live-settings.
"""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('binary', type=Path)
ap.add_argument('--paks', type=Path, required=True)
ap.add_argument('--legacy', action='store_true')
ap.add_argument('--sanitizers', action='store_true')
args = ap.parse_args()
run = ROOT / 'build/live-settings' / ('engine-' + secrets.token_hex(4))
game = run / 'id1'
game.mkdir(parents=True)
(run / 'tmp').mkdir()
files = {}
for name in ('pak0.pak', 'pak1.pak'):
    src = args.paks.resolve() / name
    (game / name).symlink_to(src)
    data = src.read_bytes()
    magic, offset, length = struct.unpack_from('<4sII', data)
    assert magic == b'PACK'
    for i in range(offset, offset + length, 64):
        entry, start, size = struct.unpack_from('<56sII', data, i)
        files[entry.split(b'\0')[0].decode()] = data[start:start + size]


def tga(path, width=64, height=64):
    path.parent.mkdir(parents=True, exist_ok=True)
    pixels = b''.join(bytes((40, 80 + (x % 8) * 20, 210 if x % 2 else 20, 255))
                      for y in range(height) for x in range(width))
    path.write_bytes(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 8) + pixels)


for mapname in ('start', 'e1m1'):
    bsp = files[f'maps/{mapname}.bsp']
    lightoffset, lightsize = struct.unpack_from('<II', bsp, 4 + 8 * 8)
    for folder, version, sample in [('', 1, bytes((120, 15, 5))),
                                     ('livealt/', 1, bytes((5, 120, 15))),
                                     ('livehdr/', 0x10001, struct.pack('<I', (23 << 27) | 180 | (30 << 9) | (10 << 18)))]:
        path = game / f'maps/{folder}{mapname}.lit'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'QLIT' + struct.pack('<I', version) + sample * lightsize)
    offset, length = struct.unpack_from('<II', bsp, 4 + 2 * 8)
    count, = struct.unpack_from('<I', bsp, offset)
    for i in range(count):
        relative, = struct.unpack_from('<i', bsp, offset + 4 + i * 4)
        if relative < 0:
            continue
        name = bsp[offset + relative:offset + relative + 16].split(b'\0')[0].decode()
        if name.lower().startswith('sky'):
            continue
        safe = '#' + name[1:] if name.startswith('*') else name
        tga(game / f'textures/{mapname}/{safe}.tga')
tga(game / 'progs/v_shot.mdl_0.tga', 96, 64)
tga(game / 'progs/s_explod.spr_0.tga', 30, 18)
# A required MD3 skin makes automatic console replacement refresh observable.
frame = struct.pack('<10f16s', -1, -1, -1, 1, 1, 1, 0, 0, 0, 2, b'fixture')
surface = (struct.pack('<4s64s10i', b'IDP3', b'fixture', 0, 1, 1, 3, 1, 108, 120, 188, 212, 236)
           + struct.pack('<3i', 0, 1, 2) + struct.pack('<64si', b'progs/live_settings_md3', 0)
           + struct.pack('<6f', 0, 0, 1, 0, 0, 1)
           + b''.join(struct.pack('<3h2B', x, y, 0, 0, 0) for x, y in [(0, 0), (64, 0), (0, 64)]))
(game / 'progs/v_shot.md3').write_bytes(
    struct.pack('<4si64s9i', b'IDP3', 15, b'fixture', 0, 1, 0, 1, 0, 108, 164, 164, 400) + frame + surface)
(game / 'progs/soldier.md3').write_bytes((game / 'progs/v_shot.md3').read_bytes())
tga(game / 'progs/live_settings_md3.tga', 32, 32)

wait = 'wait\n' * 12
config = ('cl_demoreel 0\ncl_discord_presence 0\ncl_pong 0\n'
          'cl_web_download_url ""\ncl_web_download_url2 ""\n'
          'host_maxfps 144\nvid_vsync 0\ndeveloper 1\ncon_notifylines 0\n'
          'gl_load24bit 1\ngl_load24bit_hud 0\ngl_loadlitfiles 0\n'
          'r_drawviewmodel 0\ncrosshair 0\nviewsize 120\ngamma 1\ncontrast 1\n'
          'r_dynamic 0\nr_scenecache 1\nsnd_mixspeed 44100\nsnd_surround 0\n')
if args.legacy:
    config += 'tex_parallel 0\nr_scenecache 0\n'
commands = config + 'map start\n' + wait + 'cmd pext\n' + wait * 8 + 'pause\n' + wait
labels = []


def stage(label, change=''):
    global commands
    labels.append(label)
    commands += change + wait + f'echo SETTING_{label}\nimagelist\nsoundinfo\nsave setting_{label}\nhunk_print\nscreenshot tga\n' + wait


stage('base')
stage('vsync', 'vid_vsync 1\n')
stage('vsync_off', 'vid_vsync 0\n')
stage('picmip', 'gl_picmip 2\n')
stage('picmip_off', 'gl_picmip 0\n')
stage('max_size', 'gl_load24bit 0\ngl_max_size 16\n')
stage('max_size_off', 'gl_max_size 0\ngl_load24bit 1\n')
stage('fastturb', 'r_fastturb 1\n')
stage('fastturb_off', 'r_fastturb 0\n')
stage('lit', 'gl_loadlitfiles 1\n')
stage('lit_off', 'gl_loadlitfiles 0\n')
stage('lit_alt', 'external_lits_dir livealt\ngl_loadlitfiles 2\n')
stage('lit_hdr', 'external_lits_dir livehdr\n')
stage('lit_restored', 'external_lits_dir ""\ngl_loadlitfiles 0\n')
for fmt in ('rgba8', 'bgra8', 'rgb10_a2', 'rgb9_e5', 'invalid', ''):
    stage('format_' + (fmt or 'default'), f'r_lightmap_format "{fmt}"\n')
stage('surround', 'snd_surround 1\n')
stage('surround_off', 'snd_surround 0\n')
for rate in (11025, 22050, 48000, 44100):
    stage(f'rate_{rate}', f'snd_mixspeed {rate}\n')
stage('coalesced', 'snd_mixspeed 11025\nsnd_mixspeed 44100\ngl_loadlitfiles 1\ngl_loadlitfiles 0\n')
stage('warm')
for i in range(20):
    commands += ('gl_loadlitfiles 1\nr_lightmap_format rgba8\n' + wait +
                 'external_lits_dir livehdr\ngl_loadlitfiles 2\nr_lightmap_format rgb9_e5\n' + wait +
                 'external_lits_dir ""\ngl_loadlitfiles 0\nr_lightmap_format ""\n' + wait)
stage('stress')
stage('replacement', 'r_drawviewmodel 1\nr_replacemodels md3\n')
stage('replacement_off', 'r_replacemodels ""\n')
# Menu previews and disconnected changes use the same callbacks.
commands += 'menu_graphics\ngl_picmip 1\n' + wait + 'gl_picmip 0\n' + wait
commands += 'togglemenu\ndisconnect\ngl_loadlitfiles 1\nr_lightmap_format rgba8\n' + wait
commands += 'skill 0\nmap e1m1\n' + wait + 'cmd pext\n' + wait * 8 + 'echo SETTING_MAPCHANGE\n'
# The all-skill soldier at (248,2392,40) is directly ahead from this viewpoint.
# Keep it animated while repeatedly replacing its many-pose MDL with one MD3
# pose. r_speeds verifies alias geometry actually reaches the drawing path.
commands += 'r_drawviewmodel 0\ngod\nnotarget\nnoclip\nsetpos 248 2312 48 5 90 0\n' + wait * 8
commands += 'save monster_before\necho MONSTER_BASE\nr_speeds 1\n' + wait * 2
commands += 'echo MONSTER_REPLACED\nr_replacemodels md3\n' + wait * 2
commands += 'echo MONSTER_RESTORED\nr_replacemodels ""\n' + wait * 2
for i in range(8):
    commands += 'r_replacemodels md3\n' + wait + 'r_replacemodels ""\n' + wait
commands += 'r_speeds 0\necho MONSTER_FINISHED\n'
commands += 'gl_loadlitfiles 0\nr_lightmap_format ""\n' + wait
commands += 'vid_unlock\nvid_width 336\nvid_restart\n' + wait * 2
commands += 'load setting_base\n' + wait + 'cmd pext\n' + wait * 8
commands += 'gl_loadlitfiles 1\n' + wait + 'echo SETTING_FINISHED\nquit\n'
(game / 'config.cfg').write_text(config)
(game / 'autoexec.cfg').write_text(commands)
env = {**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy',
       'LIBGL_ALWAYS_SOFTWARE': '1', 'TMPDIR': str(run / 'tmp'), 'XDG_CACHE_HOME': str(run / 'cache')}
if args.sanitizers:
    env['ASAN_OPTIONS'] = 'detect_leaks=0:abort_on_error=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
logpath = run / 'engine.log'
print(f'Running live settings: {run}', flush=True)
with logpath.open('w') as log:
    result = subprocess.run([str(args.binary.resolve()), '-basedir', str(run), '-nohome', '-window',
                             '-width', '320', '-height', '240', '-nojoy', '-nomouse', '-nolan', '-noipx',
                             '-noquakeimport', *(['-novbo', '-noglsl', '-notexturenpot'] if args.legacy else [])],
                            cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1200)
text = logpath.read_text(errors='replace')
assert result.returncode == 0 and 'SETTING_FINISHED' in text, text[-6000:]
assert not re.search(r'invalid source|TexMgr_FreeTexture:|Host_Error|Sys_Error|Segmentation|AddressSanitizer|runtime error:', text), text[-6000:]


def chunk(label):
    return re.split(r'SETTING_' + re.escape(label) + r'\s*\n', text, maxsplit=1)[1].split('SETTING_', 1)[0]


def images(label):
    # A slow debug startup can cache the console background. UI refresh drops
    # it, and a closed console need not request it again.
    return {(name.strip()): (int(w), int(h)) for w, h, name in
            re.findall(r'^\s+(\d+) x\s*(\d+) (.+)$', chunk(label), re.M)
            if name.strip() != 'gfx/conback.lmp'}


base = images('base')
for label in ('vsync', 'vsync_off', 'picmip_off', 'max_size_off', 'fastturb_off', 'stress'):
    assert images(label) == base, (label, 'texture sizes/names did not restore')
for name, size in base.items():
    if name.startswith('textures/start/') and '/#' not in name:
        assert images('picmip')[name] == (16, 16), (name, images('picmip')[name])
assert any(name.startswith('maps/start.bsp:') and size == (16, 16)
           for name, size in images('max_size').items()), 'indexed world textures did not adopt max size'
liquids = {name: size for name, size in images('fastturb').items() if name.startswith('maps/start.bsp:*')}
assert liquids and set(liquids.values()) == {(1, 1)}, liquids
assert not any(name.startswith('textures/start/#') for name in images('fastturb'))
assert 'progs/live_settings_md3' in images('replacement'), 'console replacement did not load MD3 skin'
assert 'progs/live_settings_md3' not in images('replacement_off'), 'console replacement did not restore MDL'
assert 'progs/v_shot.mdl_0' in images('replacement_off'), 'original viewmodel skin was not restored'
saved = []
for label in labels:
    save = (game / 'saves' / f'setting_{label}.sav').read_text()
    saved.append(re.sub(r'^"frametime" "[^"]*"$', '', save, flags=re.M))
assert len(set(saved)) == 1, 'live setting changed gameplay state'
used = [re.search(r'^\s*\d+ :\s*(\d+) : USED$', chunk(label), re.M).group(1) for label in ('warm', 'stress')]
assert used[0] == used[1], ('hunk grew during lighting/format switches', used)
frames = [int(frame) for frame in re.findall(r'GL_BuildLightmaps: build .*frame (\d+)\)', chunk('warm'))]
assert len(frames) == 60 and all(b > a for a, b in zip(frames, frames[1:])), ('lighting rebuild reset frame counters', frames)
shots = sorted((game / 'screenshots').glob('*.tga'))
assert len(shots) == len(labels), (len(shots), len(labels))
pixels = {label: path.read_bytes()[18:] for label, path in zip(labels, shots)}
assert pixels['lit'] != pixels['base'] and pixels['lit_alt'] != pixels['lit'], 'colored lighting did not affect pixels'
assert pixels['lit_hdr'] != pixels['lit_alt'], 'HDR lighting did not affect pixels'
assert pixels['lit_off'] == pixels['fastturb_off'] == pixels['lit_restored'], 'original lighting pixels did not restore'
for index, label in enumerate(labels):
    if label.startswith('format_'):
        assert 'GL_BuildLightmaps: build' in chunk(labels[index - 1]), f'{label} did not rebuild lightmaps'
for rate in (11025, 22050, 48000, 44100):
    assert f'{rate} Hz' in chunk(f'rate_{rate}'), f'mix rate {rate} did not apply'
assert 'Shutting down SDL sound' not in chunk('surround'), 'surround restarted audio'
assert 'Shutting down SDL sound' not in chunk('coalesced'), 'coalesced rates unnecessarily restarted audio'
assert len(re.findall(r'SV_SpawnServer total:', text)) == 3, 'settings reloaded the map'
assert text.split('SETTING_MAPCHANGE', 1)[0].count('GL_VENDOR:') == 1, 'settings restarted the renderer'
monster_save = (game / 'saves/monster_before.sav').read_text()
soldiers = [ent for ent in re.findall(r'\{[^}]*\}', monster_save) if '"model" "progs/soldier.mdl"' in ent]
assert any(re.search(r'^"frame" "[1-9]\d*(?:\.0+)?"$', ent, re.M) for ent in soldiers), 'monster did not animate before replacement'
monster_polys = {}
for label, next_label in [('BASE', 'REPLACED'), ('REPLACED', 'RESTORED')]:
    part = re.split('MONSTER_' + label + r'\s*\n', text, maxsplit=1)[1]
    part = re.split('MONSTER_' + next_label + r'\s*\n', part, maxsplit=1)[0]
    monster_polys[label] = [int(n) for n in re.findall(r'\s(\d+) epoly', part)]
    if label == 'REPLACED':
        assert re.search(r"R_AliasSetupFrame: no such frame [1-9]\d* for 'progs/soldier.mdl'", part), 'animated replacement did not reach frame setup'
assert monster_polys['BASE'] and max(monster_polys['BASE']) > 1, ('monster was not drawn', monster_polys)
assert monster_polys['REPLACED'] and max(monster_polys['REPLACED']) > 0, ('one-pose replacement was not drawn', monster_polys)
report = {'result': 'PASS', 'legacy': args.legacy, 'sanitizers': args.sanitizers, 'run': str(run),
          'checks': ['live VSync without texture changes', 'picmip/max size sizes restore', 'fastturb liquid selection',
                     'LDR/alternate/HDR lighting pixels restore', 'lightmap format cycles',
                     'surround keeps stream', 'mix rates apply', 'coalesced rates',
                     'saved gameplay unchanged', 'stable hunk across 60 lighting rebuilds',
                     'console replacement models without flush', 'animated soldier to one-pose replacement during drawing',
                     'menu/disconnect/map/save/video restart lifecycle']}
(run / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
