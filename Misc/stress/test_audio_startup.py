"""Check saved audio settings open once, then still accept live rate changes.

python3 Misc/stress/test_audio_startup.py /path/to/QSS-M --paks /path/to/id1
Runs in isolated data directories; never rewrites the supplied Quake data.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def run(binary, paks, saved, flags, initial_rate, surround):
    with tempfile.TemporaryDirectory(prefix='qssm-audio-startup-') as directory:
        root = Path(directory)
        game = root / 'id1'
        configs = game / 'configs'
        configs.mkdir(parents=True)
        for name in ('pak0.pak', 'pak1.pak'):
            source = paks / name
            if source.exists():
                (game / name).symlink_to(source)
        (configs / 'config.cfg').write_text('cl_demoreel "0"\n' + saved)
        wait = 'wait\n' * 12
        (configs / 'autoexec.cfg').write_text('')
        (configs / 'audio_test.cfg').write_text(
            wait + 'echo AUDIO_STARTUP_READY\nsoundinfo\n'
            'snd_mixspeed 32000\n' + wait +
            'echo AUDIO_LIVE_READY\nsoundinfo\nquit\n')
        with (root / 'process.log').open('w') as output:
            result = subprocess.run(
                [str(binary), '-basedir', str(root), '-nohome', '-noquakeimport',
                 '-window', '-width', '640', '-height', '480', '-nojoy', '-nomouse',
                 '-nolan', '-noipx', *flags, '+exec', 'configs/audio_test.cfg'],
                cwd=root, stdout=output, stderr=subprocess.STDOUT, timeout=90)
        log = (root / 'process.log').read_text(errors='replace')
        assert result.returncode == 0, log[-4000:]
        assert 'AUDIO_STARTUP_READY' in log and 'AUDIO_LIVE_READY' in log, log[-4000:]
        startup, live = log.split('AUDIO_STARTUP_READY', 1)
        rates = re.findall(r'SDL audio spec\s*: (\d+) Hz', startup)
        assert rates == [str(initial_rate)], ('startup opened at wrong rate or reopened', rates)
        layouts = re.findall(r'SDL audio spec[^\n]*surround: (on|off)', startup)
        assert layouts == [surround], ('startup surround preference', layouts)
        assert 'Shutting down SDL sound' not in startup, startup[-4000:]
        assert re.findall(r'SDL audio spec\s*: (\d+) Hz', live) == ['32000'], live[-4000:]
        print(f'PASS: startup opens once at {initial_rate} Hz; live change opens at 32000 Hz')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--paks', type=Path, required=True)
    args = parser.parse_args()
    saved = 'snd_mixspeed "48000"\nsnd_surround "1"\n'
    for settings, flags, rate, surround in (
            (saved, [], 48000, 'on'),
            (saved, ['+snd_mixspeed', '22050', '+snd_surround', '0'], 22050, 'off'),
            ('', [], 44100, 'on'),
            ('', ['-mixspeed', '11025'], 11025, 'on'),
            ('snd_mixspeed "0"\n', [], 8000, 'on')):
        run(args.binary.resolve(), args.paks.resolve(), settings, flags, rate, surround)
