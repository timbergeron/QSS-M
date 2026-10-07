"""Fresh, isolated native initial/map-change comparisons; original data is read-only."""
import argparse, hashlib, importlib.util, json, statistics, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
OLD = ROOT.parent / 'engine-lifecycle-six-20261005'
spec = importlib.util.spec_from_file_location('lifecycle', OLD / 'benchmark.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--batch', required=True)
    ap.add_argument('--samples', type=int, default=5)
    ap.add_argument('--engines', default='baseline,ironwail,vkquake')
    ap.add_argument('--candidate')
    ap.add_argument('--warmups', type=int, default=1)
    ap.add_argument('--baseline')
    ap.add_argument('--verify-textures', action='store_true')
    args = ap.parse_args()
    sources = {
        'baseline': ('qssm', ROOT.parent / 'engine-fps-optimization-20261005/bin-fast/quakespasm.exe'),
        'ironwail': ('ironwail', b.SOURCES['ironwail']),
        'vkquake': ('vkquake', b.SOURCES['vkquake']),
    }
    if args.candidate:
        sources['candidate'] = ('qssm', Path(args.candidate).resolve())
    if args.baseline:
        sources['baseline'] = ('qssm', Path(args.baseline).resolve())
    if args.verify_textures:
        b.SPEC['qssm'] += 'tex_verify 1\n'
    names = args.engines.split(',')
    batch = ROOT / args.batch
    batch.mkdir(parents=True, exist_ok=True)
    destination = batch / 'results.json'
    if destination.exists():
        raise RuntimeError('Use a fresh batch name; results are never overwritten.')
    out = {'endpoint': 'BENCH_BEGIN_initial to CL_SignonReply: 4 (before case 4)',
           'resolution': [800,600], 'samples_per_engine': args.samples,
           'note': 'Fresh process per sample; OS/driver caches warm. Same original lifecycle script and assets.',
           'sources': {}, 'warmups': [], 'results': []}
    for label in names:
        key, exe = sources[label]
        b.ROOT = batch / label
        b.ROOT.mkdir(exist_ok=True)
        b.SOURCES[key] = exe
        b.prepare(key)
        out['sources'][label] = {'engine': key, 'exe': str(exe),
            'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
    for round_index in range(args.warmups + args.samples):
        run = round_index + 1
        order = names[(run-1)%len(names):] + names[:(run-1)%len(names)]
        for label in order:
            key, exe = sources[label]
            b.ROOT = batch / label
            b.SOURCES[key] = exe
            row = b.session(key, run, 'lifecycle', {'nq':26001,'qw':27501})
            row['label'] = label
            row['log'] = str(Path(label) / row['log'])
            out['warmups' if round_index < args.warmups else 'results'].append(row)
            destination.write_text(json.dumps(out, indent=2))
            if not row['valid']:
                raise RuntimeError('Invalid session; inspect ' + row['log'])
    for label in names:
        rows = [r for r in out['results'] if r['label'] == label]
        print(label, {k: round(statistics.median(r['metrics_ms'][k] for r in rows),2)
                      for k in ('startup','initial','change','quit')}, flush=True)

if __name__ == '__main__':
    main()
