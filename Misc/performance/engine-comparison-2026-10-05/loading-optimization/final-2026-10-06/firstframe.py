"""Summarize signon-4 and first in-game frame (BENCH_SIGNON probe) per label."""
import json, statistics, sys
d = json.load(open(sys.argv[1]))
for lab in d['sources']:
    out = {'initial': [], 'change': [], 'initial_frame': [], 'change_frame': []}
    for r in d['results']:
        if r['label'] != lab: continue
        seq = [(e['line'], e['elapsed_ms']) for e in r['events']]
        for k in ('initial', 'change'):
            b = [t for l, t in seq if l == 'BENCH_BEGIN_' + k][0]
            out[k].append(r['metrics_ms'][k])
            fs = [t for l, t in seq if l == 'BENCH_SIGNON' and t > b]
            if fs: out[k + '_frame'].append(min(fs) - b)
    print(lab, {k: round(statistics.median(v), 1) for k, v in out.items() if v})
