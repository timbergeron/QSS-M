"""Build the redesigned Quake Engine Lab report from the raw datasets.

Inputs (read only): the 2026-10-06 same-day reruns in this folder, the
installed-build reruns, the final loading A/B published under
loading-optimization/final-2026-10-06, and the 2026-10-05 investigation data.
Output: Misc/performance/engine-comparison-2026-10-05/index.html, with the
previous page kept as index-2026-10-05.html and the new raw data copied into
data-2026-10-06/.
"""
import csv, hashlib, html, json, math, re, shutil, statistics
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
OUT = REPO / 'Misc/performance/engine-comparison-2026-10-05'
NEW = OUT / 'data-2026-10-06'
ORDER = ['qssm', 'ironwail', 'vkquake', 'qss', 'fteqw', 'ezquake']
MAP_TITLES = {'aerowalk': 'Aerowalk', 'ztndm3': 'ZTNDM3', 'dm3': 'DM3 · The Abandoned Base', 'bravado': 'Bravado',
              'ctf3m2': 'CTF3M2', 'schloss': 'Schloss', 'e1m2': 'E1M2 · Castle of the Damned',
              'dm2': 'DM2 · Claustrophobopolis', 'ctf2m8': 'CTF2M8'}

old_html_path = OUT / 'index-2026-10-05.html'
if not old_html_path.exists():
    shutil.copy2(OUT / 'index.html', old_html_path)
old_html = old_html_path.read_text(encoding='utf-8')
def embedded(page, key):
    m = re.search(r'<script[^>]*id="' + key + r'"[^>]*>(.*?)</script>', page, re.S)
    return json.loads(m.group(1))
old_life = embedded(old_html, 'data')
current_html = (OUT / 'index.html').read_text(encoding='utf-8')
loading = embedded(current_html if 'id="loading-data"' in current_html else old_html, 'loading-data')
old_tables = json.loads((HERE / 'old_tables.json').read_text(encoding='utf-8'))
old_patch = json.loads((HERE / 'old_patch_data.json').read_text(encoding='cp1252'))

def stats(samples):
    v = [x for x in samples if x is not None]
    if not v:
        return {'samples': samples, 'n': 0, 'median': None, 'min': None, 'max': None}
    return {'samples': samples, 'n': len(v), 'median': statistics.median(v), 'min': min(v), 'max': max(v)}

# ---------------- Lifecycle (same-day rerun) ----------------
life = json.loads((HERE / 'lifecycle/results.json').read_text())
assert all(r['valid'] for r in life['results']), 'invalid lifecycle rows'
# QSS-M itself is re-measured with the reviewed build (same day, same harness);
# the other engines keep their rows from the full six-engine run.
life_q = json.loads((HERE / 'lifecycle-fix2/results.json').read_text())
assert all(r['valid'] for r in life_q['results'])
life['results'] = [r for r in life['results'] if r['engine'] != 'qssm'] + life_q['results']
life['engines']['qssm'] = life_q['engines']['qssm']
inst = json.loads((HERE / 'lifecycle-installed/results.json').read_text())
assert all(r['valid'] for r in inst['results'])
def life_samples(rows, eng, metric):
    mode = {'launch_map': 'launch_map', 'connect': 'connect'}.get(metric, 'lifecycle')
    rs = sorted((r for r in rows if r['engine'] == eng and r['mode'] == mode), key=lambda r: r['run'])
    return [r['metrics_ms'].get(metric) for r in rs]

old_metrics = {m['key']: m for m in old_life['metrics']}
metric_short = {'startup': 'Engine start', 'launch_map': 'Launch → map', 'initial': 'Initial load', 'change': 'Map change',
                'connect': 'Connect · local', 'connect_remote': 'Connect · Denver', 'quit': 'Quit'}
metrics = []
for key in ['startup', 'launch_map', 'initial', 'change', 'connect', 'connect_remote', 'quit']:
    om = old_metrics[key]
    m = {'key': key, 'title': om['title'], 'short': metric_short[key], 'definition': om['definition']}
    if key == 'connect_remote':
        m['stats'] = om['stats']
        m['dated'] = 'Oct 5 · QSS-M installed build'
        m['na'] = {'ezquake': 'Needs a QuakeWorld server'}
        m['note'] = ('<span class="flag old">Oct 5 data</span> This public-server test was not repeated: it depends on denver.quakeone.com being empty and on aerowalk. '
                     'The QSS-M row is the installed 1.6.9 build, not the current source. ' + html.escape(old_life['remote_note']))
        m['unranked'] = ['ezquake']
    else:
        m['stats'] = {e: stats(life_samples(life['results'], e, key)) for e in ORDER}
        b = stats(life_samples(inst['results'], 'qssm', key))
        m['before'] = {'median': b['median'], 'label': 'installed 1.6.9 build, same day', 'stats': b}
    if key == 'connect':
        m['note'] = html.escape(old_life['connection_note']) + ' ezQuake is shown for reference and left out of the ranking.'
        m['unranked'] = ['ezquake']
    metrics.append(m)

# ---------------- FPS (same-day rerun) ----------------
FPSDIR = HERE / ('fps-v2' if (HERE / 'fps-v2/fps-results.json').exists() else 'fps')
fps = json.loads((FPSDIR / 'fps-results.json').read_text())
fps_q = json.loads((HERE / 'fps-fix2/fps-results.json').read_text())
fps['samples'] = [s for s in fps['samples'] if s['engine'] != 'qssm'] + fps_q['samples']
fps['engines']['qssm'] = fps_q['engines']['qssm']
fps_inst = json.loads((HERE / 'fps-installed/fps-results.json').read_text())
maps = list(fps['acquisition']['maps'].keys())
def fps_samples(raw, eng, mp):
    rs = sorted((s for s in raw['samples'] if s['engine'] == eng and s['map'] == mp and not s['warmup'] and s['result']), key=lambda s: s['run'])
    return [s['result']['fps'] for s in rs], [s['result']['frames'] for s in rs]
fstats, frames = {}, {}
for mp in maps:
    fstats[mp] = {}
    for e in ORDER:
        v, fr = fps_samples(fps, e, mp)
        assert len(v) == 5, (e, mp, len(v))
        fstats[mp][e] = stats(v)
        frames.setdefault(mp, fr[0])
geo = lambda xs: math.exp(sum(math.log(x) for x in xs) / len(xs))
geomean = {e: geo([fstats[mp][e]['median'] for mp in maps]) for e in ORDER}
geomean_stats = {}
for e in ORDER:
    per_pass = [geo([fstats[mp][e]['samples'][i] for mp in maps]) for i in range(5)]
    s = stats(per_pass); s['median'] = geomean[e]; geomean_stats[e] = s
fps_before = {mp: statistics.median(fps_samples(fps_inst, 'qssm', mp)[0]) for mp in maps}
fps_before_geo = geo(list(fps_before.values()))

# Gameplay demos: CTF match and the ad_tears speedrun (same fresh-process method).
EXP = HERE.parent / 'fps-expanded-20261006'
workloads, demo_raw = [], {}
for key, title, fname, inst_name, note, na in (
        ('ctf', 'CTF match · ctf3m2', 'ctf-results.json', 'ctf-installed.json',
         '16 players, 51,716 frames, scr_autoid 0', {}),
        ('adtears', 'ad_tears speedrun', 'adtears-results.json', 'adtears-installed.json',
         'Arcane Dimensions 1.80p1, Sphere easy run 2:14, 9,989 frames', {'ezquake': 'No AD or protocol 999 support'})):
    raw = json.loads((EXP / fname).read_text())
    rq = json.loads((EXP / fname.replace('-results', '-fix2')).read_text())
    raw['samples'] = [s for s in raw['samples'] if s['engine'] != 'qssm'] + rq['samples']
    raw['engines']['qssm'] = rq['engines']['qssm']
    demo_raw[key] = raw
    fstats[key] = {}
    for e in ORDER:
        v = [s['result']['fps'] for s in sorted(raw['samples'], key=lambda s: s['run'])
             if s['engine'] == e and not s['warmup'] and s['result']]
        fstats[key][e] = stats(v) if v else None
        if e not in na:
            assert len(v) == 5, (key, e, len(v))
    ir = json.loads((EXP / inst_name).read_text())
    fps_before[key] = statistics.median(s['result']['fps'] for s in ir['samples'] if not s['warmup'] and s['result'])
    workloads.append({'key': key, 'title': title, 'note': note, 'group': 'demo', 'na': na})

# ---------------- Engines and builds ----------------
fps_meta = json.loads((OUT / 'fps-results.json').read_text(encoding='utf-8'))['engines']
engines = {}
for e in ORDER:
    em = fps_meta[e]
    engines[e] = {'name': em['name'], 'renderer': em['renderer'], 'version': em['version'].replace(' · installed', '')}
engines['qssm']['version'] = 'current source'
builds = []
for e in ORDER:
    builds.append({'name': engines[e]['name'] + (' · current source' if e == 'qssm' else ''), 'version': engines[e]['version'] if e != 'qssm' else '1.6.9 + uncommitted loading changes',
                   'renderer': engines[e]['renderer'], 'sha': life['engines'][e]['sha256'], 'qssm': e == 'qssm'})
builds.insert(1, {'name': 'QSS-M · map-loading head-to-head', 'version': 'loading changes before review fixes', 'renderer': 'OpenGL',
                  'sha': loading['candidate_sha256'], 'qssm': True})
builds.insert(2, {'name': 'QSS-M · installed', 'version': '1.6.9-7da539b', 'renderer': 'OpenGL', 'sha': inst['engines']['qssm']['sha256'], 'qssm': True})
QSHA = life['engines']['qssm']['sha256']
assert fps['engines']['qssm']['exe_sha256'] == QSHA, 'lifecycle and FPS must use the same QSS-M build'
assert all(raw['engines']['qssm']['sha256'] == QSHA for raw in demo_raw.values()), 'gameplay demos must use the same QSS-M build'
builds[0]['version'] = '1.6.9 + uncommitted changes (source.patch)'

# ---------------- Copy raw data ----------------
NEW.mkdir(exist_ok=True)
for src, dst in ((HERE / 'lifecycle', NEW / 'lifecycle'), (FPSDIR, NEW / 'fps'),
                 (HERE / 'lifecycle-installed', NEW / 'lifecycle-qssm-installed'), (HERE / 'fps-installed', NEW / 'fps-qssm-installed')):
    dst.mkdir(exist_ok=True)
    for name in ('results.json', 'fps-results.json'):
        if (src / name).exists():
            shutil.copy2(src / name, dst / name)
    if (src / 'logs').exists():
        shutil.copytree(src / 'logs', dst / 'logs', dirs_exist_ok=True)
for f in ('refresh.py', 'build_report.py', 'template.html'):
    shutil.copy2(HERE / f, NEW / f)
for src, dst in ((HERE / 'lifecycle-fix2', NEW / 'lifecycle-qssm-current'), (HERE / 'fps-fix2', NEW / 'fps-qssm-current')):
    dst.mkdir(exist_ok=True)
    for name in ('results.json', 'fps-results.json'):
        if (src / name).exists():
            shutil.copy2(src / name, dst / name)
    if (src / 'logs').exists():
        shutil.copytree(src / 'logs', dst / 'logs', dirs_exist_ok=True)
import subprocess, zipfile
patch = subprocess.check_output(['git', 'diff', '--', 'Quake'], cwd=REPO)
(NEW / 'qssm-source.patch').write_bytes(patch)
BIN = HERE.parent / 'map-load-finish-20261006/bin-fix2'
assert hashlib.sha256((BIN / 'quakespasm.exe').read_bytes()).hexdigest() == QSHA, 'packaged build must be the measured build'
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
with zipfile.ZipFile(NEW / 'qssm-test-build.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for f in BIN.iterdir():
        if f.suffix.lower() in ('.exe', '.dll'):
            z.write(f, f.name)
    z.writestr('qssm-source.patch', patch)
    z.writestr('README.txt', 'QSS-M test build measured on this page. Base commit ' + head + ' plus qssm-source.patch.\nexe sha256 ' + QSHA + '\nUse your own Quake assets.\n')
(NEW / 'gameplay-demos').mkdir(exist_ok=True)
for f in ('ctf-results.json', 'adtears-results.json', 'ctf-installed.json', 'adtears-installed.json', 'ctf-fix2.json', 'adtears-fix2.json', 'demorun.py', 'focusutil.py',
          'demoinfo.py', 'investigation-20261006.json', 'repeat.py', 'cold.py', 'profile_td.py', 'probe-tdframe-stages-glcount.patch',
          'glprobe_count.h', 'swaptest/swaptest.c', 'assets/ctf-assets.json'):
    shutil.copy2(EXP / f, NEW / 'gameplay-demos' / Path(f).name)
shutil.copytree(EXP / 'demoruns/logs', NEW / 'gameplay-demos/logs', dirs_exist_ok=True)
shutil.copytree(EXP / 'logs', NEW / 'gameplay-demos/investigation-logs', dirs_exist_ok=True)
with open(NEW / 'lifecycle-samples.csv', 'w', newline='', encoding='utf-8') as fh:
    w = csv.writer(fh); w.writerow(['engine', 'build', 'mode', 'run', 'metric', 'ms'])
    for label, raw in (('current', life), ('qssm-installed', inst)):
        for r in raw['results']:
            for k, v in r['metrics_ms'].items():
                w.writerow([r['engine'], label, r['mode'], r['run'], k, round(v, 3)])
with open(NEW / 'fps-samples.csv', 'w', newline='', encoding='utf-8') as fh:
    w = csv.writer(fh); w.writerow(['engine', 'build', 'map', 'run', 'warmup', 'frames', 'seconds', 'fps'])
    for label, raw in (('current', fps), ('qssm-installed', fps_inst)):
        for s in raw['samples']:
            r = s['result'] or {}
            w.writerow([s['engine'], label, s['map'], s['run'], s['warmup'], r.get('frames'), r.get('seconds'), r.get('fps')])

# ---------------- Text ----------------
def rank_of(m, e):
    vals = {k: m['stats'][k]['median'] for k in ORDER if k not in m.get('unranked', []) and m['stats'].get(k) and m['stats'][k]['median'] is not None}
    order = sorted(vals, key=vals.get)
    return order.index(e) + 1 if e in order else None, len(order)
firsts = [m for m in metrics if m['key'] != 'connect_remote' and rank_of(m, 'qssm')[0] == 1]
fps_rank = sorted(ORDER, key=lambda e: -geomean[e]).index('qssm') + 1
lm = {m['key']: m for m in metrics}
q = lambda k: lm[k]['stats']['qssm']['median']
best_other = lambda k: min((lm[k]['stats'][e]['median'], e) for e in ORDER if e != 'qssm' and e not in lm[k].get('unranked', []))
fps_leader = max(ORDER, key=lambda e: geomean[e])

hardware = 'AMD Ryzen 5 3600X · RTX 4070'
rig = [['CPU', 'AMD Ryzen 5 3600X · 12 threads'], ['GPU', 'NVIDIA GeForce RTX 4070'], ['Driver', '610.88 (Oct 6 runs)'],
       ['OS', 'Windows 11 · build 26100'], ['Window', '800 × 600 · VSync off · sound on'], ['Runs', '5 per engine per test, fresh process each']]
headline = [
    {'k': 'Lifecycle tests QSS-M wins', 'v': '<em>' + str(len(firsts)) + '</em> of 6', 'd': 'Local tests measured today; ' + ', '.join(m['short'].lower() for m in firsts) + '.'},
    {'k': 'Launch → first map', 'v': '<em>' + f"{q('launch_map'):.0f}" + '</em> ms', 'd': f"Was {lm['launch_map']['before']['median']:.0f} ms. Next: {engines[best_other('launch_map')[1]]['name']} at {best_other('launch_map')[0]:.0f} ms."},
    {'k': 'Map change', 'v': '<em>' + f"{q('change'):.0f}" + '</em> ms', 'd': f"Was {lm['change']['before']['median']:.0f} ms. Next: {engines[best_other('change')[1]]['name']} at {best_other('change')[0]:.0f} ms."},
    {'k': 'Nine-map FPS', 'v': f"{geomean['qssm']:,.0f}", 'd': f"Was {fps_before_geo:,.0f} with the installed build (+{(geomean['qssm'] / fps_before_geo - 1) * 100:.0f}%). Rank {fps_rank} of 6; {engines[fps_leader]['name']} leads at {geomean[fps_leader]:,.0f}."},
]
init_gap = q('initial') - best_other('initial')[0]
lede = ('Startup, map loading, joining a server, quitting, and rendering speed for QSS-M, Ironwail, vkQuake, QSS, FTEQW and ezQuake, '
        'all re-measured on October 6 with the current QSS-M source. QSS-M is now fastest to start, to reach a first map from launch, '
        'to change maps and to join a local server. ' +
        (f"Its first map load from the console is within {abs(init_gap):.0f} ms of {engines[best_other('initial')[1]]['name']}. " if init_gap > 0 else '') +
        'Timedemo FPS rose ' + f"{(geomean['qssm'] / fps_before_geo - 1) * 100:.0f}%" + ' after a demo-loading fix, but rendering is where QSS-M still trails.')
matrix_note = ('Connect · local ranks the five NetQuake clients only; ezQuake joins a QuakeWorld server and is shown unranked. '
               'Connect · Denver is October 5 data with the installed QSS-M 1.6.9 build and was not repeated. '
               'The original October 5 page, measured on driver 591.86, is kept as <a href="index-2026-10-05.html">index-2026-10-05.html</a>.')

steady = loading['summary'] and (f"Timedemo FPS +2–4% vs the previous build, quit {loading['quit_check']['final']['quit']:.0f} vs "
                                 f"{loading['quit_check']['baseline']['quit']:.0f} ms, all 1,700 texture uploads identical.")
fc = loading['fps_check']
fps_rows = ''.join(f"<tr><td>{m}</td><td>{fc['grid'][m]:,.0f}</td><td>{fc['final'][m]:,.0f}</td><td>{(fc['final'][m] / fc['grid'][m] - 1) * 100:+.1f}%</td></tr>" for m in fc['final'])
rj = loading['rejected_startup_sync']
rej_rows = ''.join(f"<tr><td>{m}</td><td>{rj['fps']['grid'][m]:,.0f}</td><td>{rj['fps']['loadsync'][m]:,.0f}</td><td>{(rj['fps']['loadsync'][m] / rj['fps']['grid'][m] - 1) * 100:+.1f}%</td></tr>" for m in rj['fps']['grid'])
tv = loading['texture_verification']['maps']
checks_html = (
    '<p><strong>Gameplay FPS.</strong> Native timedemos at 800 × 600, two warm-ups then three measured passes per map, builds in ABBA order. Previous build against the final build:</p>'
    '<div class="tw"><table class="data"><thead><tr><th>Demo</th><th>Before (FPS)</th><th>Final (FPS)</th><th>Change</th></tr></thead><tbody>' + fps_rows + '</tbody></table></div>'
    f"<p><strong>Quit.</strong> Nine-run A/B against the original build: {loading['quit_check']['baseline']['quit']:.1f} ms before, {loading['quit_check']['final']['quit']:.1f} ms after.</p>"
    f"<p><strong>Textures.</strong> With <code>tex_verify 1</code>, all {tv['start']['lines']} start and {tv['e1m1']['lines']} e1m1 mip upload records match the original build exactly. This compares the CPU upload data and flags, not a GPU readback.</p>"
    "<p><strong>Tests.</strong> 7 compiled regression tests pass, including <code>test_gl_name_pools.py</code> added with the review fixes.</p>"
    '<div class="links"><a href="loading-optimization/final-2026-10-06/source.patch">Source patch</a><a href="loading-optimization/final-2026-10-06/head-to-head/results.json">Head-to-head JSON</a>'
    '<a href="loading-optimization/final-2026-10-06/fps-check/results.json">FPS check</a><a href="loading-optimization/final-2026-10-06/quit-ab/results.json">Quit A/B</a>'
    '<a href="loading-optimization/final-2026-10-06/texture-verification/verification.json">Texture verification</a><a href="loading-optimization/final-2026-10-06/qssm-fast-loading-build.zip">Test build (zip)</a></div>')
rejected_html = (
    f"<p><strong>Synchronous GL debug output from startup.</strong> It removed NVIDIA's one-time stall on the first map load (initial {rj['lifecycle']['sync']['initial']:.0f} vs {rj['lifecycle']['nosync']['initial']:.0f} ms), "
    'but it keeps the driver single-threaded for the whole session. Gameplay FPS and map changes got slower, so it was not kept.</p>'
    '<div class="tw"><table class="data"><thead><tr><th>Demo</th><th>Threaded driver (FPS)</th><th>Sync debug on (FPS)</th><th>Change</th></tr></thead><tbody>' + rej_rows + '</tbody></table></div>'
    '<p>Also measured and dropped: sync only during the first load (slower on both tests), dummy texture or GL-call warm-ups at startup (no effect), serial texture preparation (slower), a longer idle before the first map (no effect), and the earlier sky, storage, flush and mesh-storage probes.</p>'
    '<div class="links"><a href="loading-optimization/final-2026-10-06/rejected-startup-sync/results.json">Lifecycle A/B</a><a href="loading-optimization/final-2026-10-06/rejected-startup-sync-fps/results.json">FPS A/B</a><a href="loading-optimization/final-2026-10-06/rejected-loadsync-and-probes.patch">Patch kept for reference</a></div>')

# Review fixes after the head-to-head (A/B in map-load-finish-20261006/ab-fix1).
abfix = json.loads((HERE.parent / 'map-load-finish-20261006/ab-fix1/results.json').read_text())['summary']
for s in loading['steps']:
    if s['title'].startswith('Local server steps every frame'):
        s['title'] = 'Local client signon messages handled between ticks'
        s['body'] = ('Each signon round trip waited for the next 72 Hz server tick. While a local client signs on and nobody else is in game, '
                     'the server now reads and answers its messages every frame between ticks, without physics, QuakeC frames or advancing time, '
                     'so the simulation keeps its normal tick budget. (Measured first as full extra server frames; reworked after review with the same timing.)')
    if s['title'].startswith('Sounds kept across map changes'):
        s['body'] = ('Sounds loaded from a PACK keep their cached samples across maps while the same PACK still provides them. Loose files reload every map, '
                     'samples built for a different output rate are rebuilt, and table slots are never handed to another name while kept.')
review_rows = ''.join('<li>' + x + '</li>' for x in [
    '<strong>Sound slots:</strong> kept entries are never reassigned to another name, so long-lived sound pointers (temp entities, menus) stay valid; past one map\'s worth the table resets as the original code did.',
    '<strong>Changed sound files:</strong> reuse is limited to PACK-backed sounds whose PACK still provides them; loose files reload every map.',
    '<strong>Audio rate changes:</strong> cached samples built for another output rate are rebuilt on use, including after a failed audio restart.',
    '<strong>Signon timing:</strong> between-tick signon steps exchange messages only, with no physics, QuakeC frame or time advance, and only while no other client is in game. With <code>host_framerate 0.01</code> loads complete normally.',
    '<strong>Directories created during a load:</strong> <code>Sys_mkdir</code> drops the load-time directory cache, so a mod that creates a folder and file mid-load sees it immediately.',
    '<strong>vid_restart:</strong> texture and buffer name pools are kept when the GL context survives and reset only for a new context, so no names are abandoned.'])
review_html = (f"<p><strong>Review fixes.</strong> A follow-up review found six edge cases; all are fixed and covered by tests. Map-loading timing with the fixes, seven runs each: "
               f"initial {abfix['new']['initial']:.0f} ms vs {abfix['old']['initial']:.0f} ms before the fixes, change {abfix['new']['change']:.0f} vs {abfix['old']['change']:.0f} ms.</p>"
               '<ul class="small muted">' + review_rows + '</ul>')

checks_html += review_html

T = {i: t for i, t in enumerate(old_tables)}
hud_rows = [['Map', '800 × 600 before', 'after', 'change', '4K before', 'after', 'change']]
for a, b in zip(old_patch['800x600']['rows'], old_patch['3840x2160']['rows']):
    hud_rows.append([a['map'], f"{a['before']['median']:,.0f}", f"{a['after']['median']:,.0f}", f"{a['gain_percent']:+.1f}%",
                     f"{b['before']['median']:,.0f}", f"{b['after']['median']:,.0f}", f"{b['gain_percent']:+.1f}%"])
hud_rows.append(['Geometric mean', f"{old_patch['800x600']['before_geomean']:,.0f}", f"{old_patch['800x600']['after_geomean']:,.0f}", f"{old_patch['800x600']['gain_percent']:+.1f}%",
                 f"{old_patch['3840x2160']['before_geomean']:,.0f}", f"{old_patch['3840x2160']['after_geomean']:,.0f}", f"{old_patch['3840x2160']['gain_percent']:+.1f}%"])
inv6 = json.loads((EXP / 'investigation-20261006.json').read_text())
dp = inv6['demo_precache']
precache_rows = [['Map', 'Before (FPS)', 'After (FPS)', 'Change']]
for m in dp['before']:
    b, a = dp['before'][m]['median'], dp['after'][m]['median']
    precache_rows.append([MAP_TITLES.get(m, m), f"{b:,.0f}", f"{a:,.0f}", f"{(a / b - 1) * 100:+.0f}%"])
pb, pa = inv6['passes_before'], inv6['passes_after']
pass_rows = [['Pass in one process', 'Before: FPS', 'slowest frame', 'After: FPS', 'slowest frame']]
for i in range(len(pa['fps'])):
    pass_rows.append([str(i + 1), f"{pb['fps'][i]:,.0f}", f"{pb['probes'][i]['max_ms']:.0f} ms", f"{pa['fps'][i]:,.0f}", f"{pa['probes'][i]['max_ms']:.0f} ms"])
st = inv6['settle']
settle_rows = [['Before the first timedemo', 'Pass 1', 'Pass 2', 'Pass 3'],
               ['Nothing (as in the benchmark)'] + [f"{v:,.0f}" for v in st['none']],
               ['3,000 idle frames, uncapped'] + [f"{v:,.0f}" for v in st['idle_3000_frames']],
               ['~2 seconds idle at 144 FPS'] + [f"{v:,.0f}" for v in st['idle_2s_capped']] + [''],
               ['Ironwail, nothing'] + [f"{v:,.0f}" for v in st['ironwail_same_process']]]
hd = inv6['hud']
def hrow(label, k):
    s = hd[k]['stages']
    return [label, f"{hd[k]['fps'][0]:,.0f}", f"{hd[k]['fps'][-1]:,.0f}", f"{s['swap']:.0f}", f"{s['screen'] - s['swap']:.0f}", f"{s['read']:.0f}"]
budget_rows = [['Aerowalk, QSS-M', 'Pass 1 FPS', 'Warm FPS', 'Swap µs', 'Other drawing µs', 'Demo read µs'],
               hrow('Normal', 'default'), hrow('HUD hidden', 'no_hud'), hrow('No world, HUD on', 'no_world_hud'), hrow('No world, no HUD', 'no_world_no_hud')]
sw = inv6['swaptest']
swap_rows = [['Minimal window, clear and swap', 'Swap µs per frame', 'Max FPS'],
             ['SDL2', f"{sum(sw['sdl2_us']) / 2:.0f}", f"{1e6 / (sum(sw['sdl2_us']) / 2):,.0f}"],
             ['SDL3 (QSS-M)', f"{sum(sw['sdl3_us']) / 2:.0f}", f"{1e6 / (sum(sw['sdl3_us']) / 2):,.0f}"],
             ['SDL3 + 70 immediate-mode quads', f"{sw['sdl3_70quads_us']:.0f}", f"{1e6 / sw['sdl3_70quads_us']:,.0f}"]]
gl = inv6['glcalls_per_frame']
gl_rows = [['Draw function', 'glBegin blocks per frame']] + [[k, str(v)] for k, v in gl['glBegin_sites'].items()]
oct6 = {
    'date': 'October 6, 2026 · driver 610.88',
    'title': 'Why QSS-M\'s timedemo FPS was low',
    'findings': [
        {'h': 'Map loading was inside the timer (fixed)', 'p': 'QSS-M loaded a demo\'s models, sounds and lightmaps during the first timed frames, about 100 ms on Aerowalk. Other engines load while parsing the first message, before timing starts. Loading demo precaches up front raised fresh-process FPS by 26–33%.'},
        {'h': 'The first ~2 seconds run in single-threaded driver mode', 'p': 'NVIDIA switches its OpenGL driver to threaded mode about two seconds after an engine starts rendering, with one 60–80 ms stall. A fresh-process timedemo runs entirely before that switch. Every engine pays it, but QSS-M\'s per-frame driver work makes it pay more.'},
        {'h': 'The HUD is the next target', 'p': 'Every frame pays a fixed ~190 µs windowed present on this PC, the same with SDL2 or SDL3. Beyond that, QSS-M\'s immediate-mode HUD costs about 26 µs per frame; hiding it lifts warm FPS from 3,097 to 3,391, past Ironwail. Batching 2D drawing is the planned fix.'},
    ],
    'panels': [
        {'title': 'Demo precache moved out of the timed frames', 'sub': 'shipped · fresh-process FPS, five passes per map', 'intro': 'Same source and harness, before and after loading demo precaches inside the serverinfo frame (cl_parse.c). Demos can\'t download files, so nothing is lost by loading early.', 'tables': [{'caption': 'Median FPS, one warm-up process then five fresh processes per map', 'rows': precache_rows}, {'caption': 'Aerowalk, passes in one process: the before build has a 90–100 ms load frame in every pass', 'rows': pass_rows}],
         'links': [['Investigation data', 'data-2026-10-06/gameplay-demos/investigation-20261006.json'], ['Probe patch', 'data-2026-10-06/gameplay-demos/probe-tdframe-stages-glcount.patch']]},
        {'title': 'Driver warm-up in fresh processes', 'sub': 'why pass 1 is slower than pass 3', 'intro': 'The one-time stall lands wherever the driver switches modes. Idling about two seconds first, even at only 144 FPS, moves it before the first pass; frame count alone does not. Ironwail warms up across passes the same way.', 'tables': [{'caption': 'Aerowalk FPS per pass, same process', 'rows': settle_rows}]},
        {'title': 'Frame budget', 'sub': 'per-frame CPU time, µs, warm pass', 'intro': 'Timers around each stage of QSS-M\'s frame. The swap includes the driver processing the frame\'s GL commands. A bare clear-and-swap test shows ~190 µs is the windowed present floor on this PC for any engine.', 'tables': [{'caption': 'QSS-M stage timings', 'rows': budget_rows}, {'caption': 'Present floor test', 'rows': swap_rows}, {'caption': 'Immediate-mode HUD draws per frame', 'rows': gl_rows}],
         'after': 'Renaming the executable (quakespasm.exe, bench.exe, ironwail.exe) changed nothing, so no NVIDIA application profile is involved. Sound mixing costs about 3 µs per frame.',
         'links': [['Swap test source', 'data-2026-10-06/gameplay-demos/swaptest.c'], ['Profiler script', 'data-2026-10-06/gameplay-demos/profile_td.py']]},
    ],
}
oct5 = {
    'date': 'October 5, 2026 · driver 591.86',
    'title': 'Earlier investigation',
    'findings': [
        {'h': 'Fresh processes hide warm speed', 'p': 'In one process, QSS-M on Aerowalk went from 2,102 FPS on the first timedemo to 2,840 FPS on later passes. The ranking above uses first passes in fresh processes, which is where that warm-up cost shows.'},
        {'h': 'Presentation is the biggest CPU stage', 'p': 'About 230 µs per frame is spent inside the buffer swap at 800 × 600, against roughly 27 µs of GPU render time. The swap interval includes driver work and waiting, so it isn\'t pure Windows overhead.'},
        {'h': 'Small fixes, no big FPS win yet', 'p': 'The observer HUD now caches its icons, and six targeted renderer experiments were measured. None established a consistent gain; the remaining gap needs a driver-level trace.'},
    ],
    'panels': [
        {'title': 'Warm process effect', 'sub': 'QSS-M only · Aerowalk · eight passes per process', 'intro': 'Same executable, same PC. The first pass in each process is much slower than the passes after it, in every condition tried, including scene caching off.', 'tables': [T[5]],
         'links': [['Diagnostic results', 'fps/diagnostics/results.json'], ['Benchmark cvar query', 'fps/diagnostics/settings-query.log']]},
        {'title': 'Warmed comparison across engines', 'sub': 'Aerowalk · one process per engine', 'intro': 'Two discarded warm-ups, then six timedemos in one process per engine. QSS-M closes much of the gap once warm, but ezQuake and Ironwail still lead. vkQuake completed only its first pass and is excluded.', 'tables': [T[6]],
         'links': [['Warmed comparison JSON', 'fps/optimization/comparison.json']]},
        {'title': 'Where a frame\'s time goes', 'sub': 'CPU and GPU stage timings, µs', 'intro': 'Instrumented builds timed each stage of a frame. CPU and GPU intervals overlap, so they must not be added together.', 'tables': [T[7], T[1], T[2]],
         'links': [['Frame-stage JSON', 'fps/optimization/video-profile.json'], ['GPU timing JSON', 'fps/optimization/hud-cache/gpu-results.json'], ['Timedemo profile JSON', 'fps/optimization/probes/timedemo-render.json'], ['CPU stack sample', 'fps/optimization/cpu-profile.txt']]},
        {'title': 'Targeted renderer experiments', 'sub': 'six changes tried, none kept', 'intro': 'Each experiment ran against its own baseline in the same executable, with counterbalanced order and eight measured samples per mode and resolution.', 'tables': [dict(T[3], wrap=[3]), T[4]],
         'after': 'Demo parsing and rewind tracking cost about 15–18 µs per frame, far less than rendering and presentation.'},
        {'title': 'Observer HUD cache', 'sub': 'shipped · 360 timedemo passes', 'intro': 'The observer HUD searched for an optional weapon picture and resolved nine icons every frame. It now caches the result until the HUD or game reloads. Nine maps, ABBA build order, six measured passes per build, map and resolution.', 'tables': [{'caption': 'Median FPS per map, before and after the cache', 'rows': hud_rows}],
         'after': 'Gains are under 1% on the geometric mean and the before/after ranges overlap on most maps, so this is a cleanup rather than an FPS win.',
         'links': [['Before/after JSON', 'fps/optimization/hud-cache/gpu-results.json'], ['Source patch', 'fps/optimization/hud-cache/source.patch'], ['Regression test', 'fps/optimization/hud-cache/test_observer_hud_cache.py']]},
    ],
}
investigation = {'sections': [oct6, oct5]}

method = {
    'sessions': ''.join('<p>' + p + '</p>' for p in [
        'Each engine gets five lifecycle sessions (ready, load start, change to e1m1, quit), five separate launches straight into start, and five separate connections from the menu to a local server. Engine order rotates each round, and every session is a fresh process from an isolated portable copy.',
        'All copies use the same stock pak0/pak1 bytes, an 800 × 600 client area checked from the window itself, FOV 90, VSync and MSAA off, sound on, and a 144 FPS limit. OS file and driver caches stay warm; this is not a cold-boot test.',
        'Quakespasm-family clients run with <code>-listen 8</code> so they don\'t throttle while unfocused, bound to loopback. FTEQW runs with <code>-noupdates</code> so its update prompt can\'t block a session. ezQuake\'s actions are scheduled through native <code>serverexec</code> stages and <code>f_spawn</code> callbacks, because its startup ignores <code>wait</code>.',
        'The local NetQuake server is the installed QSS-M 1.6.9 dedicated build on 127.0.0.1:26001, running e1m1 with protocol 15, for every NetQuake client. ezQuake joins a separate FTE QuakeWorld server on 127.0.0.1:27501.',
        'Times come from Python <code>perf_counter</code>, with native console logs polled every 1 ms. Medians and ranges describe these five local runs, not statistical significance. Commands, configs, hashes, every observed event and exit codes are in the raw JSON.',
        'October 6 reruns use NVIDIA driver 610.88; the October 5 page used 591.86. Every engine was re-measured on October 6, so the standings compare like with like.']),
    'fps': ''.join('<p>' + p + '</p>' for p in [
        'The demos are protocol-15 NetQuake recordings made in QSS-M: a stationary camera turning about 360° over 20 seconds at the first deathmatch spawn of each map. Stock id1 game code, no monsters, no other players. CTF maps test their geometry, not CTF gameplay.',
        'One engine runs at a time. Each map and engine gets one discarded warm-up and five measured timedemos, each in a fresh process, with engine order rotated between passes. Startup and map loading fall outside the engines\' timedemo timers.',
        'vkQuake renders with Vulkan; the others use OpenGL. Renderer defaults, particles and interpolation differ between engines, so image quality and workload are close but not identical. "ms / frame" is 1000 ÷ FPS. These runs don\'t measure 1% lows, stutter, input latency or network play.',
        'The cross-map figure is the geometric mean of each map\'s median FPS, so every map counts equally.']),
    'denver': ''.join('<p>' + p + '</p>' for p in [
        'Fresh client, menu settle, then a hostname <code>connect</code> to denver.quakeone.com:26000 timed to native signon stage 4. The server (CRMod 7 on QSS-M 1.6.9) ran aerowalk with nobody connected before each attempt, and every client had the same map and 35 custom sounds cached.',
        'Protocol extensions were disabled where supported. Real network and server scheduling are included. ezQuake can\'t join a NetQuake server, so it has no result here.',
        'This test ran on October 5 with the installed QSS-M build and was not repeated for the October 6 refresh.']),
}

raw_links = [
    {'group': 'October 6 reruns (this page)', 'items': [
        ['Lifecycle JSON', 'data-2026-10-06/lifecycle/results.json'], ['Lifecycle CSV', 'data-2026-10-06/lifecycle-samples.csv'],
        ['FPS JSON', 'data-2026-10-06/fps/fps-results.json'], ['FPS CSV', 'data-2026-10-06/fps-samples.csv'],
        ['QSS-M installed · lifecycle', 'data-2026-10-06/lifecycle-qssm-installed/results.json'], ['QSS-M installed · FPS', 'data-2026-10-06/fps-qssm-installed/fps-results.json'],
        ['QSS-M · current build lifecycle', 'data-2026-10-06/lifecycle-qssm-current/results.json'], ['QSS-M · current build FPS', 'data-2026-10-06/fps-qssm-current/fps-results.json'],
        ['QSS-M source patch (this build)', 'data-2026-10-06/qssm-source.patch'], ['QSS-M test build (zip)', 'data-2026-10-06/qssm-test-build.zip'],
        ['Rerun driver script', 'data-2026-10-06/refresh.py'], ['Report build script', 'data-2026-10-06/build_report.py']]},
    {'group': 'Map loading work', 'items': [
        ['Head-to-head JSON', 'loading-optimization/final-2026-10-06/head-to-head/results.json'], ['Per-step A/B results', 'loading-optimization/final-2026-10-06/steps/'],
        ['Source patch', 'loading-optimization/final-2026-10-06/source.patch'], ['Test build (zip)', 'loading-optimization/final-2026-10-06/qssm-fast-loading-build.zip']]},
    {'group': 'October 5 originals', 'items': [
        ['Previous page', 'index-2026-10-05.html'], ['Lifecycle JSON', 'results.json'], ['Lifecycle CSV', 'samples.csv'], ['FPS JSON', 'fps-results.json'], ['FPS CSV', 'fps-samples.csv'],
        ['Lifecycle script', 'lifecycle/benchmark.py'], ['FPS script', 'fps/measure.py'], ['Recording script', 'fps/record.py'], ['Demo hashes and camera positions', 'fps/demos.json'], ['Reproduction notes', 'README.md']]},
]

data = {
    'meta': {'date_label': 'Measured October 6, 2026 · Windows 11', 'measured': 'October 6, 2026', 'lede': html.escape(lede), 'rig': rig,
             'footer': '<span>Quake Engine Lab · ' + hardware + '</span><span>Every chart value comes from the raw files linked under Method &amp; data.</span>'},
    'engines': engines, 'engine_order': ORDER, 'headline': headline,
    'lifecycle': {'metrics': metrics, 'matrix_note': matrix_note},
    'loading': {'date': 'October 6, 2026', 'summary': loading['summary'], 'steps': loading['steps'], 'steady': steady,
                'note': ('Measured with the build before the review fixes listed under Checks; a seven-run A/B shows the same timing with the fixes. Fresh process per run, one warm-up per engine, nine measured rounds in rotating order. QSS, FTEQW and ezQuake were further behind on these tests and were not part of this head-to-head. '
                         f"In the five-run six-engine suite above, the same build's initial load was {q('initial'):.0f} ms against {engines[best_other('initial')[1]]['name']}'s {best_other('initial')[0]:.0f} ms: "
                         "QSS-M's first load varies more between runs (a one-time NVIDIA driver stall), so treat initial load as a near tie with Ironwail. Map changes are clearly fastest in both."),
                'checks_html': checks_html, 'rejected_html': rejected_html},
    'fps': {'maps': [{'key': mp, 'title': MAP_TITLES.get(mp, mp), 'note': f"{frames[mp]:,} frames", 'group': 'suite'} for mp in maps] + workloads,
            'stats': fstats, 'geomean': geomean, 'geomean_stats': geomean_stats, 'before': fps_before, 'before_geomean': fps_before_geo,
            'before_label': 'installed 1.6.9, same day',
            'definition': 'Native timedemo frames per second for a 20-second recorded 360° camera turn, first pass in a fresh process. The nine-map figure is the geometric mean of per-map medians.',
            'note': 'Timedemo measures playback throughput in a fresh process; it is not a prediction of live-match FPS. Gameplay demos use the same method; QSS-M runs them with scr_autoid 0, and Ironwail and vkQuake get no unfocused sleep. See the FPS investigation for warm-process results.'
                    + (' Excluded and re-measured: Ironwail on Aerowalk, whose first four passes ran capped at 60 FPS (24 s each) while its last two ran near 3,000 FPS. The capped passes are kept under excluded_samples in the FPS JSON.' if fps.get('excluded_samples') else '')},
    'investigation': investigation, 'method': method, 'builds': builds, 'raw': raw_links,
}

# Standings use the same ranking rule as rank_of (unranked engines excluded).
for m in data['lifecycle']['metrics']:
    m.setdefault('unranked', [])

page = (HERE / 'template.html').read_text(encoding='utf-8')
blob = json.dumps(data, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')
page = page.replace('/*DATA*/', blob)
(OUT / 'index.html').write_text(page, encoding='utf-8')
print('wrote', len(page), 'bytes')
print('firsts', [m['key'] for m in firsts], 'fps rank', fps_rank)
for m in metrics:
    print(m['key'], {e: (round(m['stats'][e]['median']) if m['stats'].get(e) and m['stats'][e]['median'] is not None else None) for e in ORDER}, 'before', round(m['before']['median']) if m.get('before') else '')
print('fps geo', {e: round(geomean[e]) for e in ORDER}, 'before', round(fps_before_geo))
