#!/usr/bin/env python3
"""Render MDL/MD3/MD5 weapons/chase bodies and check surface-only opacity.

Usage: python3 Misc/stress/test_viewmodel_opacity.py --bin /path/to/QSS-M
Uses disposable basedirs, original id1 PAKs, and localhost RCON.
The oracle compares the same front surface with/without a hidden rear surface,
including a rear surface in a separate MD3/MD5 mesh. Removing self-occlusion
must fail the faded/ring comparisons while opaque comparisons still pass.
"""
import argparse
import json
import secrets
import struct
import sys
import tempfile
import time
from pathlib import Path

sys.dont_write_bytecode = True
import qssm_stress as S
import alias_visual_probe as V

ROOT = Path(__file__).resolve().parents[2]
CHASE_FIXTURE = False
TRIS = ((0, 1, 2), (0, 2, 3), (2, 1, 0), (3, 2, 0))


def vertices(back=False):
    if CHASE_FIXTURE:
        return [(8 if back else 0, y, z)
                for y, z in ((-16, -16), (16, -16), (16, 40), (-16, 40))]
    scale = 1.5 if back else 1
    return [(16 * scale, y * scale, z * scale)
            for y, z in ((-4, -9), (4, -9), (4, -3), (-4, -3))]


def make_mdl(layers):
    points = [p for back in range(layers) for p in vertices(back)]
    scale, yoffset = (.25, 32) if CHASE_FIXTURE else (.125, 8)
    header = struct.pack('<4si3f3ff3f8if', b'IDPO', 6,
                         scale, scale, scale, 0, -yoffset, -16, 64, 0, 0, 0,
                         1, 16, 16, len(points), len(TRIS) * layers, 1, 0, 0, 1)
    skin = struct.pack('<i', 0) + bytes([100]) * 256
    uv = struct.pack('<3i', 0, 8, 8) * len(points)
    indices = b''.join(struct.pack('<4i', 1, *(i + back * 4 for i in tri))
                       for back in range(layers) for tri in TRIS)
    packed = b''.join(struct.pack('<4B', round(x / scale), round((y + yoffset) / scale),
                                 round((z + 16) / scale), 0) for x, y, z in points)
    frame = struct.pack('<i4B4B16s', 0, 0, 0, 0, 0, 255, 255, 255, 0, b'idle') + packed
    return header + skin + uv + indices + frame


def make_md3(layers):
    surfaces = []
    for back in range(layers):
        triangles = b''.join(struct.pack('<3i', *tri) for tri in TRIS)
        shader = struct.pack('<64si', b'progs/opacity', 0)
        uv = struct.pack('<8f', *([.5, .5] * 4))
        points = b''.join(struct.pack('<3h2B', *(round(c * 64) for c in p), 0, 0)
                          for p in vertices(back))
        a, b, c = 108, 108 + len(triangles), 108 + len(triangles) + len(shader)
        d, end = c + len(uv), c + len(uv) + len(points)
        surfaces.append(struct.pack('<4s64s10i', b'IDP3', b'plane', 0, 1, 1,
                                    4, len(TRIS), a, b, c, d, end)
                        + triangles + shader + uv + points)
    frame = struct.pack('<10f16s', 0, -8, -16, 32, 8, 0, 0, 0, 0, 64, b'idle')
    return (struct.pack('<4si64s9i', b'IDP3', 15, b'opacity', 0, 1, 0, layers, 0,
                        108, 164, 164, 164 + sum(map(len, surfaces)))
            + frame + b''.join(surfaces))


def make_md5(layers):
    text = 'MD5Version 10\nnumJoints 1\nnumMeshes %d\njoints { "root" -1 ( 0 0 0 ) ( 0 0 0 ) }\n' % layers
    for back in range(layers):
        text += 'mesh {\nshader "opacity"\nnumverts 4\n'
        text += ''.join(f'vert {i} ( .5 .5 ) {i} 1\n' for i in range(4))
        text += f'numtris {len(TRIS)}\n'
        text += ''.join(f'tri {i} {a} {b} {c}\n' for i, (a, b, c) in enumerate(TRIS))
        text += 'numweights 4\n'
        text += ''.join(f'weight {i} 0 1 ( {x} {y} {z} )\n'
                        for i, (x, y, z) in enumerate(vertices(back)))
        text += '}\n'
    return text.encode()


def install(game, fmt, layers):
    progs = game / 'progs'
    progs.mkdir(exist_ok=True)
    # The same fixture replaces the axe and shotgun so QC weapon selection
    # cannot accidentally select an ordinary model instead of our fixture.
    data = {'mdl': make_mdl, 'md3': make_md3, 'md5mesh': make_md5}[fmt](layers)
    for name in (('player',) if CHASE_FIXTURE else ('v_shot', 'v_axe')):
        (progs / f'{name}.{fmt}').write_bytes(data)
        if fmt == 'md5mesh':
            (progs / f'{name}.md5anim').write_text(
                'MD5Version 10\nnumFrames 1\nnumJoints 1\nframeRate 10\nnumAnimatedComponents 0\n'
                'hierarchy { "root" -1 0 0 }\nbounds { ( 0 -8 -16 ) ( 32 8 0 ) }\n'
                'baseframe { ( 0 0 0 ) ( 0 0 0 ) }\nframe 0 { }\n')
    tga = (struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 16, 16, 24, 0)
           + bytes([100, 160, 100]) * 256)
    for name in ('opacity', 'opacity_00_00'):
        (progs / f'{name}.tga').write_bytes(tga)


def capture(cfg, work, fmt, layers, legacy, port):
    work.mkdir(parents=True)
    S.Sandbox(work / 'base', cfg.paks, autoexec_extra=[
        'cl_web_download_url ""', 'cl_web_download_url2 ""',
        f'r_replacemodels "{fmt if fmt != "mdl" else ""}"',
        f'chase_active {int(CHASE_FIXTURE)}', 'chase_up 0',
        'listen 1', 'cl_bob 0', 'v_idlescale 0', 'r_particles 0', 'r_dynamic 0',
        'r_drawworld 0', 'gl_clear 1', 'r_clearcolor 2', 'showpause 0',
        'r_drawflame 0', 'r_outline 0', 'gl_powerupshells 0', 'cl_damagehue 0', 'r_fastsky 1',
        'viewsize 120', 'crosshair 0', 'scr_showfps 0', 'scr_clock 0',
        'con_notifytime 0', 'r_waterwarp 0',
        'rcon_password opacitytest', 'vid_vsync 0', 'host_maxfps 72', f'vid_fsaa {cfg.fsaa}',
    ]).build()
    (work / 'base/id1/quake.rc').write_text(S.QUAKE_RC + 'stuffcmds\n')
    install(work / 'base/id1', fmt, layers)
    if CHASE_FIXTURE:
        maps = work / 'base/id1/maps'
        maps.mkdir(exist_ok=True)
        (maps / 'start.pts').write_text(''.join(
            f'{x} 300 {z}\n' for x in range(536, 553, 2) for z in range(46, 65, 2)))
    eng = S.Engine(cfg, work, port, extra_args=['-condebug'] + (['-noglslalias'] if legacy else []))
    images = {}

    def send(command):
        assert eng.alive(), eng.logpath
        replies = [eng.rcon.send(part.strip(), want_reply=True) or ''
                   for part in command.split(';')]
        return '\n'.join(replies)

    def screenshot(mode):
        directory = work / 'base/id1/screenshots'
        before = set(directory.glob('*.tga'))
        send('screenshot tga 90')
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            new = set(directory.glob('*.tga')) - before
            if new and all(p.stat().st_size >= 640 * 480 * 3 + 18 for p in new):
                image = V.read_tga(next(iter(new)))
                cropped, _ = V.crop_image(image, (.45, .4, .1, .2) if CHASE_FIXTURE else (.4, .65, .2, .25))
                (work / 'visuals').mkdir(exist_ok=True)
                V.write_tga(work / 'visuals' / (mode + '.tga'), cropped)
                return cropped
            time.sleep(.05)
        raise AssertionError(f'{mode}: no screenshot in {directory}')

    try:
        eng.start(['map start'])
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            if 'Player pos:' in send('viewpos'):
                break
            time.sleep(.2)
        else:
            raise AssertionError(f'could not spawn: {eng.logpath}')
        send('god; setpos 544 288 32 0 90 0')
        time.sleep(.25)
        modes = [('opaque', 'r_drawviewmodel 1'),
                              ('faded', 'r_drawviewmodel .5'),
                              ('faded-sort0', 'r_alphasort 0; r_drawviewmodel .5'),
                              ('fullbright-faded', 'r_alphasort 1; r_fullbright 1; r_drawviewmodel .5'),
                              ('hidden', 'r_drawviewmodel 0')]
        if CHASE_FIXTURE:
            modes = [('opaque', 'chase_active 1; chase_back 90'),
                     ('faded', 'chase_back 250'),
                     ('faded-sort0', 'r_alphasort 0'),
                     ('fullbright-faded', 'r_alphasort 1; r_fullbright 1'),
                     ('particle-free', 'r_fullbright 0; r_particles 0'),
                     ('particles', 'r_particles 1; pointfile'),
                     ('hidden', 'r_particles 0; chase_active 0; r_drawviewmodel 0')]
        if CHASE_FIXTURE and getattr(cfg, 'restart', False):
            modes.insert(-2, ('after-restart', 'vid_unlock; vid_restart'))
        for mode, command in modes:
            send(command)
            time.sleep(.2)
            images[mode] = screenshot(mode)
            if CHASE_FIXTURE:
                (work / (mode + '-viewpos.txt')).write_text(send('viewpos'))
        # Exercise the real ring -> viewent alpha path as well as explicit opacity.
        if not CHASE_FIXTURE:
            send('r_fullbright 0; r_drawviewmodel 1; give ring')
            time.sleep(.45)
            images['ring'] = screenshot('ring')
        return images
    finally:
        eng.kill()
        logs = list((work / 'base').rglob('qconsole.log'))
        text = '\n'.join(p.read_text(errors='replace') for p in [eng.logpath, *logs])
        assert not any(pattern.search(text) for pattern, _ in S.FATAL_PATTERNS), eng.logpath


def main():
    global CHASE_FIXTURE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, default=ROOT / 'macOS/build/Debug/QSS-M.app/Contents/MacOS/QSS-M')
    parser.add_argument('--paks', default=str(Path.home() / 'Desktop/qssm/id1/paks'))
    parser.add_argument('--results', type=Path, default=ROOT / '.codex-build/viewmodel-opacity')
    parser.add_argument('--formats', nargs='+', default=['mdl', 'md3', 'md5mesh'])
    parser.add_argument('--legacy', action='store_true')
    parser.add_argument('--chase', action='store_true', help='test the chase camera player proximity fade')
    parser.add_argument('--fsaa', type=int, default=0, help='scene MSAA sample request')
    parser.add_argument('--restart', action='store_true', help='recreate the chase depth cache through vid_restart')
    args = parser.parse_args()
    CHASE_FIXTURE = args.chase
    args.results = args.results.resolve()
    args.results.mkdir(parents=True, exist_ok=True)
    args.results = Path(tempfile.mkdtemp(prefix='legacy-' if args.legacy else 'glsl-', dir=args.results))
    print(f'Artifacts: {args.results}', flush=True)
    cfg = S.Config(binary=args.bin.resolve(), paks=S.find_paks(args.paks), nosound=True,
                   rcon_password="opacitytest", hang_timeout=15, hang_retries=1, memory_limit_mb=0, cpu_limit=120, fsaa=args.fsaa, restart=args.restart)
    print(json.dumps(S.binary_provenance(cfg.binary)), flush=True)
    report = []
    for fmt in args.formats:
        captures = [capture(cfg, args.results / f'{fmt}-{layers}', fmt, layers,
                            args.legacy, 29000 + secrets.randbelow(2000)) for layers in (1, 2)]
        if CHASE_FIXTURE:
            for index, shots in enumerate(captures):
                particles, _ = V.compare_images(shots['particle-free'], shots['particles'], 2, 1)
                assert particles['changed_pixels'] > 100, (fmt, index, 'background particles hidden by player depth', particles)
                print(fmt, index + 1, particles['changed_pixels'], 'background particle pixels', flush=True)
                if args.restart:
                    restarted, _ = V.compare_images(shots['particle-free'], shots['after-restart'], 2, 1)
                    assert restarted['changed_pixels'] <= 20, (fmt, index, 'opacity changed after video restart', restarted)
        # Ensure the fixture really drew, including the faded state. Otherwise
        # a skipped/failed model could make hidden-layer equality pass vacuously.
        modes = ['opaque', 'faded', 'faded-sort0', 'fullbright-faded']
        if not CHASE_FIXTURE:
            modes.append('ring')
        elif args.restart:
            modes.append('after-restart')
        for mode in modes:
            visible, _ = V.compare_images(captures[0][mode], captures[0]['hidden'], 2, 1)
            assert visible['changed_pixels'] > (50 if CHASE_FIXTURE else 500), (fmt, mode, 'fixture not visible', visible)
            diff, _ = V.compare_images(captures[0][mode], captures[1][mode], 2, 1)
            report.append({'format': fmt, 'mode': mode, **diff})
            print(fmt, mode, diff['changed_pixels'], 'changed pixels', flush=True)
    (args.results / 'report.json').write_text(json.dumps(report, indent=2))
    failures = [r for r in report if r['changed_pixels'] > 20]
    assert not failures, f'hidden geometry changed the viewmodel: {[(r["format"], r["mode"], r["changed_pixels"]) for r in failures]}'
    print('PASS: hidden geometry does not affect chase opacity; background particles remain visible' if CHASE_FIXTURE else
          'PASS: hidden geometry does not affect opaque, faded, or ring viewmodels (both alpha sorts and fullbright)')


if __name__ == '__main__':
    main()
