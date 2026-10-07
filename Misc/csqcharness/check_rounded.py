#!/usr/bin/env python3
"""Pixel-check drawroundedrect in CSQC and MenuQC at two UI scales.

Run after run_harness.py has generated the platform headers:
  python3 check_rounded.py --bin /path/to/quakespasm --basedir /path/to/quake \
      --fteqcc /path/to/fteqcc --headers /path/to/csqcharness/src
Uses temporary game directories and preserves installed configs.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

QC = r'''
float frames;
float shown;
void(vector viewport) rr_draw =
{
    frames = frames + 1;
    drawfill('0 0 0', viewport, '0 0 0', 1, 0);
    drawroundedrect('16 16 0', '64 48 0', '1 1 1', 1, 12);
    drawroundedrect('104 16 0', '64 48 0', '1 1 1', 1, 0);
    drawroundedrect('192 16 0', '64 48 0', '1 1 1', 1, 12, 1);
    drawroundedrect('16 88 0', '64 48 0', '1 1 1', 1, 999);
    // Full optional args immediately before defaults catches stale VM parms.
    drawroundedrect('-64 -64 0', '32 32 0', '1 1 1', 1, 12, 1, 2);
    drawroundedrect('104 88 0', '64 48 0', '1 1 1', 1, 12);
    drawroundedrect('192 88 0', '64 48 0', '0.2 1 0.4', 0.5, 12);
    // Both clipping and textured blending must survive the native fill.
    drawsetcliparea(16, 160, 32, 24);
    drawroundedrect('16 160 0', '64 48 0', '1 1 1', 1, 0);
    drawresetcliparea();
    drawroundedrect('104 160 0', '64 48 0', '0 0 0', 1, 12);
    drawpic('104 160 0', "gfx/half.tga", '64 48 0', '1 1 1', 1, 0);
    if (frames == 120) localcmd("screenshot tga\n");
    if (frames == 125) localcmd("quit\n");
};
#ifdef CSQC
void(vector viewport, float showscores) CSQC_DrawHud = { rr_draw(viewport); };
void(float api, string engine, float version) CSQC_Init = {};
#else
void(float mode) m_toggle = { shown = 1; setkeydest(2); };
void(vector viewport) m_draw = { if (shown) rr_draw(viewport); };
void() m_init = {};
void(float key, float character) m_keydown = {};
void(float key, float character) m_keyup = {};
#endif
'''


def read_tga(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<HH', data, 12)
    bpp = data[16] // 8
    assert data[2] == 2 and bpp in (3, 4), 'expected uncompressed screenshot'
    def pixel(x, y):
        if not data[17] & 32:
            y = height - y - 1
        offset = 18 + data[0] + (y * width + x) * bpp
        return tuple(data[offset + i] for i in (2, 1, 0))
    return pixel


def check(path, scale):
    pixel = read_tga(path)
    def sample(x, y):
        return pixel(int(x * scale), int(y * scale))
    def near(x, y, expected, tolerance=3):
        actual = sample(x, y)
        assert all(abs(a-b) <= tolerance for a, b in zip(actual, expected)), (x, y, actual, expected)
    white, black = (255, 255, 255), (0, 0, 0)
    near(16, 16, black)
    near(48, 40, white)
    near(79, 63, black)
    near(104, 16, white)  # radius zero
    near(192, 16, black)  # TL-only mask
    near(255, 16, white)
    near(192, 63, white)
    near(16, 88, black)  # clamped capsule
    near(48, 112, white)
    near(167, 135, black)  # default mask ignores the preceding TL-only call
    near(224, 112, (26, 128, 51))
    near(20, 164, white)
    near(50, 164, black)  # clipping survived
    near(136, 184, (128, 128, 128))  # alpha test/blend state survived
    # The curved edge must contain coverage values between black and white.
    values = [pixel(x, y)[0] for y in range(16*scale, 29*scale)
              for x in range(16*scale, 29*scale)]
    assert any(4 < value < 251 for value in values), 'rounded edge lacks antialiasing'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bin', 'basedir', 'fteqcc', 'headers'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, default=Path('artifacts/rounded'))
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy',
           'LIBGL_ALWAYS_SOFTWARE': '1', 'LP_NUM_THREADS': '2'}
    with tempfile.TemporaryDirectory(prefix='qssm-rounded-') as temp:
        root = Path(temp)
        (root / 'id1').mkdir()
        for name in ('pak0.pak', 'pak1.pak'):
            source = args.basedir.resolve() / 'id1' / name
            if source.exists(): (root / 'id1' / name).symlink_to(source)
        for name in ('quakespasm.pak', 'qssm.pak'):
            source = args.bin.resolve().parent / name
            if source.exists(): (root / name).symlink_to(source)
        for module in ('cs', 'menu'):
            for scale in (1, 2):
                game = root / f'{module}{scale}'
                (game / 'gfx').mkdir(parents=True)
                name = 'csprogs.dat' if module == 'cs' else 'menu.dat'
                header = 'qscsextensions.qc' if module == 'cs' else 'qsmenuextensions.qc'
                shutil.copyfile(args.headers / header, game / header)
                (game / 'test.qc').write_text(QC)
                (game / 'progs.src').write_text(f'{name}\n{header}\ntest.qc\n')
                built = subprocess.run([str(args.fteqcc.resolve()), '-Wall', '-srcfile', 'progs.src'],
                                       cwd=game, capture_output=True, text=True, timeout=30)
                assert built.returncode == 0 and 'warning' not in built.stdout.lower(), built.stdout
                # RGBA fixture: half-transparent white, in top-origin TGA.
                tga = bytearray(18)
                tga[2], tga[16], tga[17] = 2, 32, 0x28
                struct.pack_into('<HH', tga, 12, 8, 8)
                (game / 'gfx/half.tga').write_bytes(tga + bytes((255, 255, 255, 128)) * 64)
                (game / 'autoexec.cfg').write_text(
                    f'scr_sbarscale {scale}\nscr_menuscale {scale}\n'
                    'con_notifytime 0\ncon_notifylines 0\nscr_ping 0\nscr_clock 0\n'
                    'scr_showfps 0\ncl_afk 0\nscr_fade 0\nscr_conspeed 100000\n'
                    'host_maxfps 100\nviewsize 120\n')
                command = [str(args.bin.resolve()), '-basedir', str(root), '-game', game.name,
                           '-nohome', '-nolan', '-noudp', '-nojoy', '-nomouse', '-window',
                           '-width', '640', '-height', '480', '-nosound']
                command += ['+map', 'start'] if module == 'cs' else ['+togglemenu', '1']
                run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
                (args.artifacts / f'{module}{scale}.log').write_text(run.stdout + run.stderr)
                assert run.returncode == 0, run.stdout[-2000:]
                shots = list((game / 'screenshots').glob('*.tga'))
                assert shots, run.stdout[-2000:]
                destination = args.artifacts / f'{module}{scale}.tga'
                shutil.copyfile(shots[-1], destination)
                check(destination, scale)
                print(f'{module} scale {scale}: PASS (corners, radius, alpha, defaults, clipping, texture state)')


if __name__ == '__main__':
    main()
