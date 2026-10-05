#!/usr/bin/env python3
"""Windows startup/map/video/audio/quit smoke test using an isolated basedir.

python windows_lifecycle.py path/to/quakespasm.exe --paks path/to/id1
Writes artifacts under .codex-build/windows-lifecycle; never modifies source data.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
import re
from pathlib import Path
import secrets
import shutil
import socket
import struct
import subprocess
import time

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('exe', type=Path)
ap.add_argument('--paks', required=True, type=Path)
ap.add_argument('--label', default='final')
ap.add_argument('--nosound', action='store_true')
ap.add_argument('--audio-failure', action='store_true', help='inject an unavailable SDL audio driver')
ap.add_argument('--fullscreen', action='store_true', help='exercise desktop and exclusive fullscreen before returning to a window')
ap.add_argument('--engine-arg', action='append', default=[], help='additional engine option (use --engine-arg=-option)')
ap.add_argument('--immediate-quit', action='store_true')
args = ap.parse_args()
repo = Path(__file__).resolve().parents[2]
base = repo / '.codex-build' / 'windows-lifecycle' / args.label
assert base.resolve().is_relative_to(repo / '.codex-build' / 'windows-lifecycle'), 'label must stay inside the artifact directory'
(base / 'id1').mkdir(parents=True, exist_ok=True)
# The engine resolves its log and user directory beside the executable.
exe = base / args.exe.name
shutil.copy2(args.exe.resolve(), exe)
for dll in args.exe.resolve().parent.glob('*.dll'):
    shutil.copy2(dll, base / dll.name)
for name in ('pak0.pak', 'pak1.pak', 'hud24bit.pak'):
    dest, src = base / 'id1' / name, args.paks.resolve() / name
    if not dest.exists() and src.exists():
        os.link(src, dest)
(base / 'id1' / 'config.cfg').write_text('''cl_demoreel "0"
vid_width "800"
vid_height "600"
vid_vsync "0"
vid_fullscreen "0"
vid_fxaa "0"
scr_fade "0"
gl_load24bit "1"
gl_load24bit_hud "1"
cl_discord_presence "0"
con_notifydiscord ""
''')
log = base / 'qconsole.log'
log.unlink(missing_ok=True)
old_screenshots = set((base / 'id1' / 'screenshots').glob('*.png'))
env = dict(os.environ)
env.pop('QSSM_PROF_QUIT', None)
if args.audio_failure:
    env['SDL_AUDIO_DRIVER'] = 'qssm-unavailable-test-driver'
password = secrets.token_hex(12)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]
command = [str(exe), '-basedir', str(base), '-noquakeimport', '-condebug',
    '-window', '-ip', '127.0.0.1', '-port', str(port), '+rcon_password', password, '+listen', '1']
if args.nosound: command.append('-nosound')
command.extend(args.engine_arg)
if args.immediate_quit:
    command.extend(['+toggleconsole', '+quit'])
else:
    steps = [
        'vid_describemodes; menu_video', 'vid_width 960; vid_height 720; vid_restart', 'map start',
    ]
    waits = 'wait\n' * 60
    (base / 'id1' / 'lifecycle.cfg').write_text(waits + waits.join(step + '\n' for step in steps))
    stale = base / 'id1' / 'maps' / 'lifecycle.tmp'
    stale.parent.mkdir(exist_ok=True)
    stale.write_bytes(b'stale test download')
    command.extend(['+exec', 'lifecycle.cfg'])
p = subprocess.Popen(command, cwd=base, env=env)

def contents():
    return log.read_text(errors='replace') if log.exists() else ''

def send(command):
    body = b'\x05' + password.encode() + b'\0' + command.encode() + b'\0'
    packet = struct.pack('>I', 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(packet, ('127.0.0.1', port))

def wait_until(test, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if test(): return
        if p.poll() is not None: raise AssertionError(f'engine exited early: {p.returncode}')
        time.sleep(.02)
    raise AssertionError('timed out waiting for lifecycle step')

def batch(command):
    marker = 'lifecycle_' + secrets.token_hex(6)
    # Alias expansion queues commands for the normal frame, after RCON redirection.
    send(f'alias lifecycle_batch "{command}; echo {marker}"')
    time.sleep(.05)
    send('lifecycle_batch')
    wait_until(lambda: marker in contents())

def sound_probe():
    before = len(contents())
    batch('soundinfo')
    data = contents()[before:]
    if not args.nosound and not args.audio_failure:
        assert 'sound system not started' not in data and 'samplepos' in data, data

user32 = ctypes.WinDLL('user32', use_last_error=True)
enum_cb = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.EnumWindows.argtypes = [enum_cb, wintypes.LPARAM]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

def visible_window():
    windows = []
    @enum_cb
    def each(hwnd, unused):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == p.pid and user32.IsWindowVisible(hwnd): windows.append(hwnd)
        return True
    user32.EnumWindows(each, 0)
    return windows[0] if windows else None

try:
    if args.immediate_quit:
        p.wait(timeout=15)
        assert p.returncode == 0
        print('PASS: immediate startup quit')
    else:
        wait_until(lambda: '#Introduction' in contents(), timeout=30)
        batch('echo lifecycle_ready')
        if args.fullscreen:
            modes = [tuple(map(int, m)) for m in re.findall(r'(\d+) x\s*(\d+) x (\d+) : (\d+)', contents())]
            width, height, bpp, rate = min((m for m in modes if m[0] >= 640 and m[1] >= 480), key=lambda m: m[0] * m[1])
            batch('vid_desktopfullscreen 1; vid_fullscreen 1; vid_restart')
            before = len(contents())
            batch('vid_describecurrentmode')
            assert 'fullscreen' in contents()[before:]
            batch(f'vid_desktopfullscreen 0; vid_width {width}; vid_height {height}; vid_bpp {bpp}; vid_refreshrate {rate}; vid_restart')
            before = len(contents())
            batch('vid_describecurrentmode')
            assert f'{width}x{height}x{bpp} {rate}Hz fullscreen' in contents()[before:]
            batch('vid_fullscreen 0; vid_width 960; vid_height 720; vid_restart')
        sound_probe()
        batch('screenshot png')
        time.sleep(.1)
        if not args.nosound and not args.audio_failure:
            batch('snd_mixspeed 22050; loadas8bit 1; snd_restart')
            sound_probe()
            batch('snd_mixspeed 48000; loadas8bit 0; snd_restart')
            sound_probe()
        batch('vid_fxaa 2; vid_restart')
        time.sleep(.3)
        batch('vid_fxaa 0; vid_restart')
        batch('disconnect; menu_main; vid_restart; map e1m1')
        wait_until(lambda: '#the Slipgate Complex' in contents())
        batch('screenshot png')
        time.sleep(.1)
        batch('sensitivity 4.321')
        assert len(set((base / 'id1' / 'screenshots').glob('*.png')) - old_screenshots) >= 2
        hwnd = visible_window()
        assert hwnd
        started = time.perf_counter()
        assert user32.PostMessageW(hwnd, 0x0010, 0, 0) # WM_CLOSE: normal confirmed close path
        hide_deadline = time.perf_counter() + 5
        while visible_window() and p.poll() is None and time.perf_counter() < hide_deadline:
            time.sleep(.001)
        assert not visible_window(), 'quit did not hide the window within five seconds'
        hide_ms = (time.perf_counter() - started) * 1000
        p.wait(timeout=15)
        exit_ms = (time.perf_counter() - started) * 1000
        assert p.returncode == 0 and not stale.exists()
        assert '4.321' in (base / 'id1' / 'config.cfg').read_text()
        text = contents()
        assert 'GLSL program failed' not in text and 'GLSL shader failed' not in text
        assert 'shader compilation failed' not in text and 'shader linking failed' not in text
        if args.audio_failure:
            assert "Couldn't init SDL audio" in text
        result = {'window_hidden_ms': round(hide_ms, 1), 'process_exited_ms': round(exit_ms, 1)}
        (base / 'result.json').write_text(json.dumps(result, indent=2))
        audio_check = 'audio failure recovery' if args.audio_failure else 'sound disabled' if args.nosound else 'sound restarts'
        print(f'PASS: startup, modes/menu, disconnected/connected video restarts, maps, FXAA, {audio_check}, persistence, temp cleanup, quit', result)
finally:
    if p.poll() is None:
        p.kill()
        p.wait()
