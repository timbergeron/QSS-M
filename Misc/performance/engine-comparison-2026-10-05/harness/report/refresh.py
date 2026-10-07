"""Same-day rerun of the six-engine lifecycle and FPS suites with the current QSS-M build.

Reuses the original harnesses unchanged except for three substitutions:
- the QSS-M client is the final loading build (bin-final), or the installed build with --installed-qssm;
- the local NetQuake connect server stays the installed QSS-M build, as on Oct 5;
- FTEQW timedemo runs pass -noupdates, as the lifecycle runs already do.
Outputs go to this folder; the original datasets are never touched.
"""
import argparse, importlib.util, shutil, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent
FPSROOT = BUILD / 'engine-fps-20261005'
FINAL = BUILD / 'map-load-finish-20261006/bin-final/quakespasm.exe'

sys.path.insert(0, str(FPSROOT))
import measure  # the FPS harness; its SOURCES dict is shared with the lifecycle harness

INSTALLED_QSSM = measure.SOURCES['qssm']
CLIENT = FINAL
measure.SOURCES['qssm'] = CLIENT


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


SUFFIX, ONLY, FOCUS = '', [], False


def lifecycle():
    b = load('lifecycle', BUILD / 'engine-lifecycle-six-20261005/benchmark.py')
    b.ROOT = HERE / ('lifecycle' + SUFFIX)
    b.ROOT.mkdir(exist_ok=True)
    prepare = b.prepare

    def prepare_server_installed(key, role=None):
        if role == 'server-nq':
            b.SOURCES['qssm'] = INSTALLED_QSSM
            try:
                return prepare(key, role)
            finally:
                b.SOURCES['qssm'] = CLIENT
        return prepare(key, role)

    b.prepare = prepare_server_installed
    sys.argv = ['benchmark.py'] + (['--only', ','.join(ONLY)] if ONLY else [])
    b.main()


def fps():
    measure.ROOT = HERE / ('fps' + SUFFIX)
    measure.ROOT.mkdir(exist_ok=True)
    for name in ('assets', 'demos'):
        if not (measure.ROOT / name).exists():
            shutil.copytree(FPSROOT / name, measure.ROOT / name)
    for name in ('demos.json',):
        shutil.copy2(FPSROOT / name, measure.ROOT / name)
    real = subprocess.Popen

    class Popen(real):
        def __init__(self, args, *a, **k):
            if Path(args[0]).name.lower().startswith('fteqw') and '-noupdates' not in args:
                args = list(args) + ['-noupdates']
            super().__init__(args, *a, **k)

    measure.subprocess.Popen = Popen
    if FOCUS:
        sys.path.insert(0, str(BUILD / 'fps-expanded-20261006'))
        from focusutil import focus
        geometry = measure.window_pixels
        focused = set()
        def window_pixels(pid):
            g = geometry(pid)
            if g and pid not in focused and focus(pid):
                focused.add(pid)
            return g
        measure.window_pixels = window_pixels
    sys.argv = ['measure.py'] + (['--only', ','.join(ONLY)] if ONLY else [])
    measure.main()


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('suite', choices=['lifecycle', 'fps'])
    ap.add_argument('--installed-qssm', action='store_true', help='measure only the installed QSS-M build into *-installed folders')
    ap.add_argument('--qssm-exe', help='QSS-M client to measure instead of bin-final')
    ap.add_argument('--suffix', default='', help='output folder suffix, e.g. -v2')
    ap.add_argument('--focus', action='store_true', help='bring each engine window to the foreground')
    ap.add_argument('--only', help='comma-separated engine keys to measure')
    a = ap.parse_args()
    FOCUS = a.focus
    if a.qssm_exe:
        CLIENT = Path(a.qssm_exe).resolve()
        measure.SOURCES['qssm'] = CLIENT
    SUFFIX = a.suffix
    if a.only:
        ONLY = a.only.split(',')
    if a.installed_qssm:
        CLIENT = INSTALLED_QSSM
        measure.SOURCES['qssm'] = CLIENT
        SUFFIX, ONLY = '-installed', ['qssm']
    {'lifecycle': lifecycle, 'fps': fps}[a.suite]()
