#!/usr/bin/env python3
"""Check live MSAA on a loaded map, using an isolated Quake basedir.

python3 Misc/stress/test_live_fsaa_engine.py Quake/quakespasm --paks /path/to/id1
SDL_VIDEODRIVER=offscreen works with headless Mesa. Artifacts stay under build/.
"""
import argparse
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("exe", type=Path)
ap.add_argument("--paks", required=True, type=Path)
ap.add_argument("--engine-arg", action="append", default=[])
ap.add_argument("--startup-fsaa", type=int)
ap.add_argument("--config-aa", type=int, nargs=2, default=(0, 0), metavar=("MSAA", "FXAA"))
ap.add_argument("--fxaa-first", action="store_true")
ap.add_argument("--fteqcc", type=Path, help="also compile and exercise full CSQC AA changes")
ap.add_argument("--expect-no-msaa", action="store_true", help="require MSAA requests to fall back to zero samples")
args = ap.parse_args()
base = ROOT / "build/live-fsaa" / ("engine-" + secrets.token_hex(4))
(base / "id1").mkdir(parents=True)
for name in ("pak0.pak", "pak1.pak"):
    src = args.paks.resolve() / name
    if src.exists():
        (base / "id1" / name).symlink_to(src)
for name in ("qssm.pak", "quakespasm.pak"):
    (base / name).symlink_to(ROOT / "Quake" / name)
exe = base / args.exe.name
shutil.copy2(args.exe.resolve(), exe)
aa_config = [f'vid_fsaa "{args.config_aa[0]}"', f'vid_fxaa "{args.config_aa[1]}"']
if args.fxaa_first:
    aa_config.reverse()
config = "\n".join(aa_config) + '\n' + '''vid_width "640"
vid_height "480"
vid_fullscreen "0"
vid_vsync "0"
gamma "1"
contrast "1"
scr_fade "0"
cl_demoreel "0"
cl_discord_presence "0"
host_maxfps "60"
'''
(base / "id1/config.cfg").write_text(config)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]
password = secrets.token_hex(12)


def underwater_position():
    """Find a verified water-leaf center in the test map for the warp pass."""
    with (args.paks / "pak0.pak").open("rb") as pak:
        magic, offset, size = struct.unpack("<4sii", pak.read(12))
        assert magic == b"PACK"
        pak.seek(offset)
        directory = pak.read(size)
        for entry in struct.iter_unpack("<56sii", directory):
            if entry[0].split(b"\0", 1)[0] == b"maps/start.bsp":
                pak.seek(entry[1])
                bsp = pak.read(entry[2])
                break
        else:
            raise AssertionError("maps/start.bsp missing from pak0.pak")
    assert struct.unpack_from("<i", bsp)[0] == 29

    def lump(index):
        offset, size = struct.unpack_from("<ii", bsp, 4 + index * 8)
        return bsp[offset:offset + size]

    planes, nodes, leaves = lump(1), lump(5), lump(10)
    for offset in range(0, len(leaves), 28):
        contents, _, *bounds = struct.unpack_from("<ii6h", leaves, offset)
        if contents != -3:  # CONTENTS_WATER
            continue
        point = [(bounds[i] + bounds[i + 3]) / 2 for i in range(3)]
        node = 0
        while node >= 0:
            plane, front, back = struct.unpack_from("<ihh", nodes, node * 24)
            nx, ny, nz, dist = struct.unpack_from("<4f", planes, plane * 20)
            node = front if sum(a * b for a, b in zip(point, (nx, ny, nz))) >= dist else back
        if struct.unpack_from("<i", leaves, (-1 - node) * 28)[0] == -3:
            point[2] -= 22  # player origin is below the camera
            return " ".join(str(value) for value in point)
    raise AssertionError("no water-leaf center found")


def rcon(cmd):
    body = b"\x05" + password.encode() + b"\0" + cmd.encode() + b"\0"
    packet = struct.pack(">I", 0x80000000 | (len(body) + 4)) + body
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(4)
        sock.sendto(packet, ("127.0.0.1", port))
        result = []
        try:
            while True:
                result.append(sock.recv(65536)[5:].rstrip(b"\0").decode(errors="replace"))
                sock.settimeout(.05)
        except socket.timeout:
            return "".join(result)


def active(request, enabled=None):
    match = None
    result = ""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        assert process.poll() is None, ("engine exited", process.returncode, str(log))
        result = rcon("vid_describecurrentmode")
        match = re.search(r"MSAA: requested (\d+)x, active (\d+)x", result)
        if match and int(match[1]) == request:
            samples = int(match[2])
            if enabled is None or (samples > 1) == enabled:
                return samples
        time.sleep(.05)
    raise AssertionError(("mode did not settle", request, enabled, result, process.poll(), str(log)))


def change(command, samples):
    rcon(command)
    time.sleep(.2)
    enabled = samples > 1 and not args.expect_no_msaa
    actual = active(samples, enabled=enabled)
    assert (actual > 1) == enabled, (command, samples, actual)
    if args.expect_no_msaa:
        assert actual == 0, (command, actual)
    assert "map:     start" in rcon("status"), "server/map changed"
    return actual


env = dict(os.environ)
env["MESA_DEBUG"] = "context"
if args.fteqcc:
    src = base / "id1/src"
    src.mkdir()
    (base / "id1/live_fsaa_dump.cfg").write_text("pr_dumpplatform -Tcs -O qscsextensions\nquit\n")
    with (base / "dump.log").open("w") as output:
        subprocess.run([str(exe), "-basedir", str(base), "-noquakeimport", "-window", "-nosound",
                        "+exec", "live_fsaa_dump.cfg"], cwd=base, env=env, stdout=output,
                       stderr=subprocess.STDOUT, timeout=30, check=True)
    shutil.copy2(ROOT / "Misc/stress/live_fsaa_csqc.qc", src)
    (src / "progs.src").write_text("../csprogs.dat\nqscsextensions.qc\nlive_fsaa_csqc.qc\n")
    with (base / "compile.log").open("w") as output:
        subprocess.run([str(args.fteqcc.resolve()), "-srcfile", "progs.src"], cwd=src,
                       stdout=output, stderr=subprocess.STDOUT, timeout=30, check=True)
    assert (base / "id1/csprogs.dat").exists(), base / "compile.log"
    (base / "id1/config.cfg").write_text(config)
log = base / "run.log"
with log.open("w") as output:
    startup = [] if args.startup_fsaa is None else ["-fsaa", str(args.startup_fsaa)]
    process = subprocess.Popen([str(exe), "-basedir", str(base), "-noquakeimport", "-window", "-nosound",
        "-ip", "127.0.0.1", "-port", str(port), *args.engine_arg, *startup,
        "+rcon_password", password, "+listen", "1", "+map", "start"],
        cwd=base, env=env, stdout=output, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            assert process.poll() is None, log.read_text(errors="replace")[-4000:]
            if "map:     start" in rcon("status"):
                break
        else:
            raise AssertionError("map startup timed out: " + str(log))
        time.sleep(1)
        initial = (0 if args.config_aa[1] else args.config_aa[0]) if args.startup_fsaa is None else args.startup_fsaa
        assert (active(initial) > 1) == (initial > 1 and not args.expect_no_msaa)
        print("PASS: startup request survives config loading:", initial, flush=True)
        counts = []
        for level in (0, 2, 4, 6, 8, 16, 0, 4):
            counts.append((level, change(f"vid_fsaa {level}", level)))
        print("PASS: sample requests/applied counts on the same loaded map:", counts, flush=True)
        for command in ("gamma .7", "contrast 1.5", "r_softemu 1", "r_softemu 0", "gamma 1",
                        "contrast 1", "gl_motion_blur .3", "r_scale 2", "r_scale 1",
                        "r_waterwarp 1", "crosshair 6", "crosshair 7", "crosshair 1"):
            change(command, 4)
        print("PASS: gamma, soft emulation, motion blur, render scaling, and crosshairs", flush=True)
        change("setpos " + underwater_position(), 4)
        for command in ("r_waterwarp 1", "r_scale 2", "r_scale 1", "viewsize 80", "viewsize 100",
                        "r_stereo 2", "r_stereo 0", "setpos start"):
            change(command, 4)
        print("PASS: underwater warp, small viewport, and stereo", flush=True)
        for level in (1, 2, 3, 0):
            change(f"vid_fxaa {level}", 0)
        change("vid_fsaa 4", 4)
        change("vid_fxaa 2", 0)
        change("vid_fsaa 2", 2)
        assert '"vid_fxaa" is "0"' in rcon("vid_fxaa")
        print("PASS: FXAA/MSAA selection switches live", flush=True)
        for command in ("menu_graphics", "togglemenu 0", "vid_width 800", "vid_height 600", "vid_restart"):
            change(command, 2)
        result = rcon("vid_describecurrentmode")
        assert "800x600" in result, result
        rcon("screenshot tga")
        time.sleep(.5)
        screenshots = list((base / "id1/screenshots").glob("*.tga"))
        assert screenshots and screenshots[0].stat().st_size > 10000, screenshots
        for _ in range(10):
            change("vid_fsaa 0", 0)
            change("vid_fsaa 4", 4)
        print("PASS: menu, connected video restart, screenshots, and repeated toggles", flush=True)
        if args.fteqcc:
            # SDL's offscreen driver keeps the original default framebuffer size
            # on window resize. Return to that size before testing window pixels.
            for command in ("vid_width 640", "vid_height 480", "vid_restart"):
                change(command, 4)
            rcon("scr_sbarscale 1")

            def capture():
                directory = base / "id1/screenshots"
                previous = set(directory.glob("*.tga"))
                rcon("screenshot tga")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    assert process.poll() is None, ("engine exited", process.returncode, str(log))
                    for shot in set(directory.glob("*.tga")) - previous:
                        data = shot.read_bytes()
                        if len(data) < 18:
                            continue
                        assert data[1:3] == b"\0\x02" and data[16] == 24
                        width, height = struct.unpack_from("<HH", data, 12)
                        if len(data) == 18 + data[0] + width * height * 3:
                            return data, width, height
                    time.sleep(.05)
                raise AssertionError("screenshot did not complete: " + str(directory))

            def pixel(x, y):
                row = y if data[17] & 0x20 else height - 1 - y
                offset = 18 + data[0] + (row * width + x) * 3
                return tuple(reversed(data[offset:offset + 3]))

            for phase, samples in enumerate((0, 4, 0, 0, 4, 0), 1):
                change(f"live_fsaa_qc {phase}", samples)
                data, width, height = capture()
                assert pixel(40, 40) == (255, 0, 0), (phase, pixel(40, 40))
                green = pixel(120, 40)
                assert green[0] == green[2] == 0 and 126 <= green[1] <= 129, (phase, green)
                assert f"PASS: live-fsaa full CSQC phase {phase}" in log.read_text(errors="replace")
            print("PASS: full CSQC changes AA mid-frame; UI-only frames, clip restoration, and pixels", flush=True)
            change("scr_menuscale 1", 0)
            change("menu_setup", 0)

            def preview_colors():
                # At scale 1 the 48x56 player preview starts at (368, 220).
                return {pixel(x, y) for y in range(224, 272) for x in range(372, 412)}

            change("r_drawentities 0", 0)
            data, width, height = capture()
            empty_colors = len(preview_colors())
            change("r_drawentities 1", 0)
            for command, samples in (("vid_fsaa 0", 0), ("vid_fsaa 4", 4), ("gamma .7", 4),
                                     ("gamma 1", 4), ("vid_fxaa 2", 0), ("vid_fsaa 4", 4)):
                change(command, samples)
                data, width, height = capture()
                # Distinguish the textured model from the same empty menu box.
                colors = preview_colors()
                assert len(colors) > empty_colors + 16, (command, "missing player preview", len(colors), empty_colors)
            for command, samples in (("vid_fsaa 4", 4), ("vid_fxaa 2", 0)):
                change(command, samples)
                # Mark the mode dirty, then restart at the original dimensions
                # while the preview is open and its old resources already exist.
                change("vid_width 648", samples)
                change("vid_width 640", samples)
                change("vid_restart", samples)
                data, width, height = capture()
                assert len(preview_colors()) > empty_colors + 16, (command, "preview lost after restart")
            change("vid_fsaa 4", 4)
            change("togglemenu 0", 4)
            print("PASS: player-preview pixels across AA/gamma switches and video restart while open", flush=True)
        rcon("quit")
        assert process.wait(timeout=15) == 0
        contents = log.read_text(errors="replace")
        if re.search(r"^GL_VERSION: 2\.1\b", contents, re.MULTILINE):
            # SDL's EGL probe asks GL_MAJOR_VERSION on GL 2.1 before GL_Init.
            # Exclude just that one known library error, never renderer errors.
            prefix, marker, rest = contents.partition("GL_VERSION:")
            probe = "Mesa: error: GL_INVALID_ENUM in glGetIntegerv(pname=GL_MAJOR_VERSION)\n"
            if probe in prefix:
                contents = prefix.replace(probe, "", 1) + marker + rest
                print("NOTE: excluded SDL's GL 2.1 version-probe error before renderer initialization", flush=True)
        assert not re.search(r"GL_INVALID_|GL_OUT_OF_MEMORY|Host_Error|Sys_Error|Segmentation fault", contents), contents[-4000:]
        print("PASS: clean exit; no renderer GL errors. Artifacts:", base, flush=True)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
