#!/usr/bin/env python3
"""Exercise teleporters through the real cached and uncached renderers.

Requires Linux (/proc thread inspection), an X11 OpenGL display, and Quake's
pak0.pak. Uses isolated configs and generates brush-model fixtures from
start.bsp; no game files are modified.

xvfb-run -a python3 Misc/stress/test_teleport_scenecache.py --basedir /path/to/quake
"""

import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import threading


ROOT = Path(__file__).resolve().parents[2]


def pak_member(path, wanted):
    with path.open('rb') as source:
        magic, offset, size = struct.unpack('<4sii', source.read(12))
        assert magic == b'PACK'
        source.seek(offset)
        directory = source.read(size)
        for name, offset, length in struct.iter_unpack('<56sii', directory):
            if name.split(b'\0')[0].decode() == wanted:
                source.seek(offset)
                return source.read(length)
    raise AssertionError(f'{wanted} missing from {path}')


def brush_fixture(bsp, moved=False):
    """Move the easy-difficulty portal's faces from world leaves to a func_wall.

    Identity placement exercises baked submodels; translated placement exercises
    the separate brush-entity renderer. Keep the stock map's PVS and lighting.
    """
    assert struct.unpack_from('<i', bsp)[0] == 29
    lumps = [bsp[offset:offset + size] for offset, size in
             struct.iter_unpack('<ii', bsp[4:124])]
    # Face numbers in the original shareware/registered start.bsp.
    firstface, numfaces = 4336, 4
    assert len(lumps[7]) >= (firstface + numfaces) * 20
    texinfo = list(struct.iter_unpack('<8fii', lumps[6]))
    vertices = list(struct.iter_unpack('<3f', lumps[3]))
    edges = list(struct.iter_unpack('<HH', lumps[12]))
    surfedges = [e[0] for e in struct.iter_unpack('<i', lumps[13])]
    points = []
    for i in range(firstface, firstface + numfaces):
        _, _, start, count, ti, _, _ = struct.unpack_from('<Hhihh4si', lumps[7], i * 20)
        texture = texinfo[ti][8]
        offset = struct.unpack_from('<i', lumps[2], 4 + texture * 4)[0]
        assert lumps[2][offset:offset + 5] == b'*tele'
        for e in surfedges[start:start + count]:
            points.append(vertices[edges[abs(e)][0 if e >= 0 else 1]])
    bounds = [min(p[i] for p in points) for i in range(3)]
    bounds += [max(p[i] for p in points) for i in range(3)]
    assert bounds[0] < 232 < bounds[3] and bounds[1] >= 1384
    marks = [s[0] for s in struct.iter_unpack('<H', lumps[11])]
    leaves, newmarks = bytearray(lumps[10]), []
    for offset in range(0, len(leaves), 28):
        start, count = struct.unpack_from('<HH', leaves, offset + 20)
        kept = [s for s in marks[start:start + count]
                if not firstface <= s < firstface + numfaces]
        struct.pack_into('<HH', leaves, offset + 20, len(newmarks), len(kept))
        newmarks.extend(kept)
    lumps[10] = bytes(leaves)
    lumps[11] = struct.pack(f'<{len(newmarks)}H', *newmarks)
    modelnum = len(lumps[14]) // 64
    # Reuse world collision nodes; the test player uses noclip. Rendering uses
    # the portal's own bounds and face range.
    lumps[14] += struct.pack('<9f7i', *bounds, 0, 0, 0, 0, 0, 0, 0, 0, firstface, numfaces)
    origin = '48 0 0' if moved else '0 0 0'
    entity = f'\n{{\n"classname" "func_wall"\n"model" "*{modelnum}"\n"origin" "{origin}"\n}}\n'
    lumps[0] = lumps[0].rstrip(b'\0') + entity.encode() + b'\0'
    header, payload = bytearray(struct.pack('<i', 29)), bytearray()
    for lump in lumps:
        payload.extend(b'\0' * (-len(payload) % 4))
        header.extend(struct.pack('<ii', 124 + len(payload), len(lump)))
        payload.extend(lump)
    return bytes(header + payload)


def image_pixels(path):
    data = path.read_bytes()
    assert data[0:3] == b'\0\0\x02' and data[16] == 24, 'expected uncompressed 24-bit TGA'
    width, height = struct.unpack_from('<HH', data, 12)
    pixels = data[18:]
    assert len(pixels) == width * height * 3
    # Ignore console notifications at the top. The portal is centered in the
    # remaining image.
    if data[17] & 32:
        pixels = pixels[64 * width * 3:]
    else:
        pixels = pixels[:-64 * width * 3]
    return (width, height), pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--basedir', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT / 'Quake/quakespasm')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    work = (args.output or Path(tempfile.mkdtemp(prefix='qssm-telecache-'))).resolve()
    work.mkdir(parents=True, exist_ok=True)
    game = work / 'id1'
    game.mkdir(exist_ok=True)
    for name in ('pak0.pak', 'pak1.pak'):
        source = (args.basedir / 'id1' / name).resolve()
        if source.exists() and not (game / name).exists():
            (game / name).symlink_to(source)
    assert (game / 'pak0.pak').exists(), 'Quake pak0.pak is required'
    for name in ('quakespasm.pak', 'qssm.pak'):
        if not (work / name).exists():
            (work / name).symlink_to(ROOT / 'Quake' / name)
    (game / 'maps').mkdir(exist_ok=True)
    bsp = pak_member(game / 'pak0.pak', 'maps/start.bsp')
    for name, moved in [('tc_brush', False), ('tc_moved', True)]:
        (game / 'maps' / (name + '.bsp')).write_bytes(brush_fixture(bsp, moved))

    upper = bytearray(bsp)
    textures_offset, _ = struct.unpack_from('<ii', upper, 4 + 2 * 8)
    numtextures = struct.unpack_from('<i', upper, textures_offset)[0]
    for i in range(numtextures):
        offset = struct.unpack_from('<i', upper, textures_offset + 4 + i * 4)[0]
        if offset >= 0 and upper[textures_offset + offset:textures_offset + offset + 5] == b'*tele':
            upper[textures_offset + offset:textures_offset + offset + 5] = b'*TELE'
    (game / 'maps/tc_upper.bsp').write_bytes(upper)
    base = brush_fixture(bsp, True)
    lumps = [base[offset:offset + size] for offset, size in struct.iter_unpack('<ii', base[4:124])]
    lumps[0] = lumps[0].replace(b'"classname" "func_wall"', b'"alpha" "0.5"\n"classname" "func_wall"')
    header, payload = bytearray(struct.pack('<i', 29)), bytearray()
    for lump in lumps:
        payload.extend(b'\0' * (-len(payload) % 4))
        header.extend(struct.pack('<ii', 124 + len(payload), len(lump)))
        payload.extend(lump)
    (game / 'maps/tc_alpha.bsp').write_bytes(header + payload)

    def config(name, commands):
        (game / (name + '.cfg')).write_text('\n'.join(commands) + '\n')

    config('autoexec', [
        'cl_web_download_url ""', 'cl_web_download_url2 ""', 'cl_mapshots 0',
        'cl_afk 0', 'developer 1', 'scr_fade 0', 'host_maxfps 72', 'vid_vsync 0',
        'viewsize 120', 'crosshair 0', 'r_drawviewmodel 0', 'r_scenecache 1',
        'r_telestyle 1', 'alias tc_connect "exec tc_start.cfg"', 'map start',
    ])
    # Running after signon avoids putting protocol negotiation behind the waits.
    config('connect', ['tc_connect'])
    cases = []

    def capture(label, commands):
        cases.append(label)
        return [commands] + ['wait'] * 20 + ['echo TC_CASE_' + label, 'screenshot tga'] + ['wait'] * 12

    def camera(x):
        return ['host_timescale 1'] + ['wait'] * 20 + ['noclip', f'setpos {x} 1264 24 0 90 0'] + ['wait'] * 20 + ['host_timescale 0.000001']

    commands = camera(232)
    for label, command in [
        ('classic', 'r_scenecache 1; r_telestyle 1'),
        ('mirror_cached', 'r_telestyle 3'),
        ('refract_cached', 'r_telestyle 2'),
        ('mirror_uncached', 'r_scenecache 0; r_telestyle 3'),
        ('mirror_auto', 'r_scenecache ""'),
        ('mirror_worldonly', 'r_scenecache 2'),
        ('classic_again', 'r_scenecache 1; r_telestyle 1'),
        ('mirror_again', 'r_telestyle 3'),
        # Mark video settings dirty, then restore the same size for comparison.
        ('fastturb', 'r_fastturb 1; vid_width 648; vid_width 640; vid_restart'),
        ('normal_reload', 'r_fastturb 0; vid_width 648; vid_width 640; vid_restart'),
        ('resized', 'vid_width 800; vid_height 600; vid_restart'),
    ]:
        commands += capture(label, command)
    commands += ['alias tc_connect "exec tc_brush.cfg"', 'host_timescale 1', 'map tc_brush']
    config('tc_start', commands)
    commands = camera(232)
    commands += capture('brush_cached', 'r_scenecache 1')
    commands += capture('brush_uncached', 'r_scenecache 0')
    commands += capture('brush_worldonly', 'r_scenecache 2')
    commands += ['alias tc_connect "exec tc_moved.cfg"', 'host_timescale 1', 'map tc_moved']
    config('tc_brush', commands)
    commands = camera(280)
    commands += capture('moved_cached', 'r_scenecache 1')
    commands += capture('moved_uncached', 'r_scenecache 0')
    # Exercise shared lightmaps while reflection/refraction passes are active.
    commands += ['r_scenecache 1', 'host_timescale 1', 'give 7', 'give r 50', 'impulse 7', '+attack']
    commands += ['wait'] * 120 + ['-attack']
    commands += capture('dynamic', 'r_telestyle 3')
    commands += ['alias tc_connect "exec tc_upper.cfg"', 'host_timescale 1', 'map tc_upper']
    config('tc_moved', commands)

    commands = camera(232)
    commands += capture('upper_cached', 'r_scenecache 1; r_telestyle 3')
    commands += capture('upper_uncached', 'r_scenecache 0')
    commands += capture('upper_classic', 'r_scenecache 1; r_telestyle 1')
    commands += ['alias tc_connect "exec tc_alpha.cfg"', 'host_timescale 1', 'map tc_alpha']
    config('tc_upper', commands)
    commands = camera(280)
    commands += capture('alpha_cached', 'r_scenecache 1; r_telestyle 3')
    commands += capture('alpha_uncached', 'r_scenecache 0')
    commands += [
        'alias tc_connect "exec tc_reconnect.cfg"', 'disconnect',
        'vid_width 808', 'vid_width 800', 'vid_restart',
        'host_timescale 1', 'map start',
    ]
    config('tc_alpha', commands)
    commands = camera(232)
    commands += capture('reconnected_cached', 'r_scenecache 1; r_telestyle 3')
    commands += capture('reconnected_uncached', 'r_scenecache 0')
    commands += capture('reconnected_refract', 'r_scenecache 1; r_telestyle 2')
    commands += capture('reconnected_classic', 'r_telestyle 1')
    commands += ['echo TC_DONE', 'toggleconsole', 'quit']
    config('tc_reconnect', commands)

    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='x11', SDL_AUDIODRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1')
    proc = subprocess.Popen([
        'stdbuf', '-oL', '-eL', str(args.binary.resolve()), '-basedir', str(work),
        '-window', '-width', '640', '-height', '480', '-nosound', '-nolan', '-noudp',
    ], cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors='replace')
    timeout = threading.Timer(180, proc.kill)
    timeout.start()
    observed, lastcase = {}, None
    try:
        with (work / 'run.log').open('w') as log:
            for line in proc.stdout:
                log.write(line)
                if line.startswith('TC_CASE_'):
                    lastcase = line.strip().removeprefix('TC_CASE_')
                    tids = []
                    for task in Path(f'/proc/{proc.pid}/task').glob('*'):
                        try:
                            if (task / 'comm').read_text().strip() == 'scenecache':
                                tids.append(int(task.name))
                        except FileNotFoundError:
                            pass
                    observed[lastcase] = tids
                    print(lastcase, 'cache worker', tids, flush=True)
                if line.startswith('Wrote ') and line.strip().endswith('.tga') and lastcase:
                    source = Path(line.strip().removeprefix('Wrote '))
                    assert source.resolve().is_relative_to(work)
                    shutil.copyfile(source, work / (lastcase + '.tga'))
        proc.wait()
    finally:
        timeout.cancel()
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    print('Artifacts:', work, flush=True)
    assert proc.returncode == 0 and list(observed) == cases
    log = (work / 'run.log').read_text()
    assert 'TC_DONE' in log
    for failure in ('Host_Error', 'Sys_Error', 'Teleporter framebuffer unavailable', 'failed to create worker', 'Shader compilation failed'):
        assert failure not in log, failure
    for case in cases:
        assert bool(observed[case]) == ('uncached' not in case), case
    assert observed['classic'] == observed['mirror_cached'] == observed['refract_cached']
    assert observed['mirror_auto'] == observed['mirror_worldonly'] == observed['classic_again'] == observed['mirror_again']
    assert observed['mirror_again'] != observed['fastturb'] != observed['normal_reload'], 'video restart did not rebuild the worker'
    assert image_pixels(work / 'resized.tga')[0] == (800, 600)

    def difference(left, right):
        size_a, a = image_pixels(work / (left + '.tga'))
        size_b, b = image_pixels(work / (right + '.tga'))
        assert size_a == size_b
        mean = sum(abs(x - y) for x, y in zip(a, b)) / len(a)
        print(f'{left} / {right}: mean color difference {mean:.5f}', flush=True)
        return mean

    for left, right in [
        ('mirror_cached', 'mirror_uncached'), ('mirror_cached', 'mirror_auto'),
        ('mirror_cached', 'mirror_worldonly'), ('mirror_cached', 'mirror_again'),
        ('classic', 'classic_again'), ('mirror_cached', 'fastturb'), ('mirror_cached', 'normal_reload'),
        ('brush_cached', 'brush_uncached'), ('brush_cached', 'brush_worldonly'),
        ('moved_cached', 'moved_uncached'), ('upper_cached', 'upper_uncached'), ('alpha_cached', 'alpha_uncached'),
        ('reconnected_cached', 'reconnected_uncached'),
    ]:
        assert difference(left, right) < 0.15, (left, right)
    # A new map has a different particle/player/wave clock: compare modes
    # within that load, rather than expecting cross-map pixel identity.
    assert difference('reconnected_cached', 'reconnected_classic') > 0.3, 'mirror missing after disconnected restart'
    assert difference('reconnected_cached', 'reconnected_refract') > 0.2, 'reflection missing after disconnected restart'
    assert difference('upper_cached', 'upper_classic') > 0.3, 'uppercase mirror effect is missing'
    assert difference('moved_cached', 'alpha_cached') > 0.1, 'entity alpha is ignored'
    assert difference('mirror_cached', 'classic') > 0.3, 'mirror effect is missing'
    assert difference('mirror_cached', 'refract_cached') > 0.2, 'reflection pass is missing'
    print('PASS: cache reuse, visual parity, brush models, lighting, connected/disconnected restarts, and map changes')


if __name__ == '__main__':
    main()
