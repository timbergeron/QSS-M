"""Counterbalanced native lifecycle A/B of private QSS-M builds.

--variant label=path/to/quakespasm.exe[@cvar value;cvar value][!-arg -arg]
Reuses the published lifecycle harness (endpoint, assets, script) unchanged.
"""
import argparse, hashlib, importlib.util, json, statistics
from pathlib import Path
ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('initial', ROOT.parent / 'map-load-20261005/measure_initial.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); b = m.b
ap = argparse.ArgumentParser()
ap.add_argument('--batch', required=True)
ap.add_argument('--variant', action='append', required=True)
ap.add_argument('--samples', type=int, default=5)
ap.add_argument('--warmups', type=int, default=1)
ap.add_argument('--ready-waits', type=int, default=0, help='override idle frames before the initial map (diagnostic only)')
a = ap.parse_args()
dest = ROOT / a.batch; dest.mkdir(parents=True, exist_ok=True); target = dest / 'results.json'
if target.exists(): raise RuntimeError('Use a fresh batch name')
base_spec = b.SPEC['qssm']
if a.ready_waits:
    _session = b.session
    def session_long(*x, **k):
        w = b.WAIT
        b.WAIT = 'wait\n' * a.ready_waits
        try: return _session(*x, **k)
        finally: b.WAIT = w
    b.session = session_long
variants = {}
for s in a.variant:
    label, rest = s.split('=', 1)
    rest, _, extra = rest.partition('!')
    exe, _, cfg = rest.partition('@')
    variants[label] = (Path(exe).resolve(), cfg.replace(';', '\n'), extra.split())
raw = {'sources': {}, 'results': [], 'warmups': [],
       'method': 'Fresh process per sample, rotating variant order, retained warmups; published 800x600 lifecycle script and endpoint.'}
base_command = b.command
def use(label):
    exe, cfg, extra = variants[label]
    b.command = lambda *x: base_command(*x) + extra
    b.ROOT = dest / label; b.SOURCES['qssm'] = exe
    b.SPEC['qssm'] = base_spec + (cfg + '\n' if cfg else '')
for label, (exe, cfg, extra) in variants.items():
    use(label); b.ROOT.mkdir(exist_ok=True); b.prepare('qssm')
    raw['sources'][label] = {'exe': str(exe), 'config': cfg, 'args': extra, 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
names = list(variants)
for i in range(a.warmups + a.samples):
    order = names[i % len(names):] + names[:i % len(names)]
    for label in order:
        use(label)
        row = b.session('qssm', i + 1, 'lifecycle', {'nq': 26001, 'qw': 27501})
        row['label'] = label; row['log'] = str(Path(label) / row['log'])
        raw['warmups' if i < a.warmups else 'results'].append(row)
        target.write_text(json.dumps(raw, indent=2))
        if not row['valid']: raise RuntimeError('Invalid session ' + row['log'])
raw['summary'] = {l: {k: round(statistics.median(r['metrics_ms'][k] for r in raw['results'] if r['label'] == l), 1)
                      for k in ('startup', 'initial', 'change', 'quit')} for l in names}
target.write_text(json.dumps(raw, indent=2))
for l, s in raw['summary'].items(): print(l, s, flush=True)
