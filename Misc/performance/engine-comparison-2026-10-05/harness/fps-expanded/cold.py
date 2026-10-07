"""Fresh-process FPS exactly as the published harness measures it, for one engine build.

usage: cold.py --label L --exe path [--engine qssm] [--maps aerowalk,dm3] [--passes 5]
One discarded warm-up process, then N measured processes per map (measure.run unchanged).
"""
import argparse, json, statistics, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FPSROOT = HERE.parent / 'engine-fps-20261005'
sys.path.insert(0, str(FPSROOT))
import measure

ap = argparse.ArgumentParser()
ap.add_argument('--label', required=True)
ap.add_argument('--exe', required=True)
ap.add_argument('--engine', default='qssm')
ap.add_argument('--maps', default='aerowalk,dm3,ctf2m8')
ap.add_argument('--passes', type=int, default=5)
a = ap.parse_args()

root = HERE / 'cold' / a.label
root.mkdir(parents=True, exist_ok=True)
for name in ('assets', 'demos'):
    if not (root / name).exists():
        (root / name).symlink_to(FPSROOT / name, target_is_directory=True) if False else __import__('shutil').copytree(FPSROOT / name, root / name)
measure.ROOT = root
measure.SOURCES[a.engine] = Path(a.exe).resolve()
measure.prepare(a.engine)
out = {'label': a.label, 'exe': str(Path(a.exe).resolve()), 'maps': {}}
for mp in a.maps.split(','):
    rows = [measure.run(a.engine, mp, i, i == 0) for i in range(a.passes + 1)]
    fps = [r['result']['fps'] for r in rows[1:] if r['result']]
    out['maps'][mp] = {'samples': fps, 'median': statistics.median(fps), 'warmup': rows[0]['result']['fps'] if rows[0]['result'] else None}
    print(a.label, mp, round(statistics.median(fps), 1), fps, flush=True)
res = HERE / 'cold-results.json'
allr = json.loads(res.read_text()) if res.exists() else {}
allr[a.label] = out
res.write_text(json.dumps(allr, indent=2))
