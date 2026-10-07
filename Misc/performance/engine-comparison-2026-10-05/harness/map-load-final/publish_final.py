"""Replace the report's loading panel with the final source A/B; keep every original dataset."""
import hashlib, json, re, shutil, statistics, subprocess, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
OUT = REPO / 'Misc/performance/engine-comparison-2026-10-05'
FINAL = REPO / '.codex-build/map-load-20261005/final-20261006'
TEXV = REPO / '.codex-build/map-load-20261005/texverify-final-20261006'
FPS = REPO / '.codex-build/map-load-race-20261006/fps-final'
REJECT_FPS = REPO / '.codex-build/map-load-race-20261006/fps-loadsync2'
LABELS = ('baseline', 'candidate', 'ironwail', 'vkquake')
PRETTY = {'ironwail': 'Ironwail 0.8.2', 'vkquake': 'vkQuake 1.36.0'}
KEYS = ('startup', 'initial', 'change', 'quit')

raw = json.loads((FINAL / 'results.json').read_text())
assert all(r['valid'] for r in raw['results'] + raw['warmups'])
summary = {}
for key in KEYS:
    summary[key] = {}
    for label in LABELS:
        v = [r['metrics_ms'][key] for r in raw['results'] if r['label'] == label]
        assert len(v) == 9
        summary[key][label] = {'median': statistics.median(v), 'min': min(v), 'max': max(v), 'samples': v}

def ab(batch, before, after, note, firstframe=False):
    d = json.loads((ROOT / batch / 'results.json').read_text())
    med = lambda lab, k: statistics.median(r['metrics_ms'][k] for r in d['results'] if r['label'] == lab)
    n = sum(1 for r in d['results'] if r['label'] == after)
    row = {'batch': batch, 'samples': n, 'note': note,
           'initial': [med(before, 'initial'), med(after, 'initial')],
           'change': [med(before, 'change'), med(after, 'change')]}
    if firstframe:
        frames = {}
        for lab in (before, after):
            out = {'initial': [], 'change': []}
            for r in d['results']:
                if r['label'] != lab:
                    continue
                seq = [(e['line'], e['elapsed_ms']) for e in r['events']]
                for k in out:
                    b = [t for l, t in seq if l == 'BENCH_BEGIN_' + k][0]
                    out[k].append(min(t for l, t in seq if l == 'BENCH_SIGNON' and t > b) - b)
            frames[lab] = {k: statistics.median(v) for k, v in out.items()}
        row['first_frame'] = {'initial': [frames[before]['initial'], frames[after]['initial']],
                              'change': [frames[before]['change'], frames[after]['change']]}
    return row

steps = [
    dict(title='Directory checks shared across a whole load', files='common.c, sv_main.c, cl_parse.c, gl_rmisc.c, host.c',
         body='Optional replacement textures and models miss in every loose directory. One stat per missing directory now serves the whole server spawn, client precache and R_NewMap, instead of one per candidate file. The cache ends with the load (and on Host_Error), so files added between loads are found.',
         **ab('ab-dircache', 'prev', 'dircache', 'prev = this morning\'s image-search and texture-batching build')),
    dict(title='Local server steps every frame while its client signs on', files='host.c',
         body='Each signon round trip waited for the next 72 Hz server tick. While the local client is still signing on, the server and network step every frame, advancing by real elapsed time so physics stays on the wall clock.',
         **ab('ab-signon', 'dircache', 'signon', '')),
    dict(title='No frame-rate cap during that signon', files='host.c',
         body='The screen is frozen for loading, so the last few round trips no longer wait for a 144 Hz frame slot. Normal capping resumes at signon 4.',
         **ab('ab-uncap', 'signon', 'uncap', '')),
    dict(title='Sounds kept across map changes', files='snd_dma.c, q_sound.h',
         body='Known sounds and their cached samples survive map changes, like alias models. Cache_Flush on game change or memory pressure drops samples and they reload; a full table recycles entries no later map asked for instead of failing.',
         **ab('ab-sound', 'uncap', 'sound', '')),
    dict(title='Texture names from a pool', files='gl_texmgr.c',
         body='glGenTextures returns values, so NVIDIA\'s threaded driver must stop and catch up with its worker. Names now come from a pool generated in bulk outside the load, so the first map\'s texture commit no longer blocks (61 ms -> 0.7 ms).',
         **ab('ab-names', 'sound', 'names', '')),
    dict(title='Buffer names from a pool', files='gl_rmisc.c, gl_mesh.c, r_brush.c, gl_vidsdl.c',
         body='The same applies to glGenBuffers for alias-model meshes and the brush VBO. The pool is refilled for each new GL context.',
         **ab('ab-bufnames', 'names', 'bufnames', '')),
    dict(title='Last frame held while a local client signs on', files='gl_screen.c',
         body='Map changes already freeze the screen behind the loading plaque. A first load from the console now does the same for at most two seconds, so CPU loading overlaps the driver instead of waiting on a console frame. Verified with a first presented in-game frame probe (glFinish after the first post-signon swap), not only the signon marker.',
         **ab('ab-hold2', 'draw', 'hold', 'one binary; hold toggled by a diagnostic switch; 9 samples', firstframe=True)),
]

fps = json.loads((FPS / 'results.json').read_text())
reject_fps = json.loads((REJECT_FPS / 'results.json').read_text())
quit_ab = json.loads((ROOT / 'ab-quit' / 'results.json').read_text())
reject_ab = json.loads((ROOT / 'ab-startsync' / 'results.json').read_text())
texv = json.loads((TEXV / 'verification.json').read_text())
tests = ['test_image_file_candidates.py', 'test_texture_name_batch.py', 'test_sound_precache_reuse.py',
         'test_item_color_grid.py', 'test_prepared_item_color.py', 'test_observer_hud_cache.py']

art = OUT / 'loading-optimization' / 'final-2026-10-06'
art.mkdir(parents=True, exist_ok=True)
for label in LABELS:
    shutil.copytree(FINAL / label / 'logs', art / 'head-to-head' / label / 'logs', dirs_exist_ok=True)
shutil.copy2(FINAL / 'results.json', art / 'head-to-head' / 'results.json')
for s in steps:
    dest = art / 'steps' / s['batch']
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / s['batch'] / 'results.json', dest / 'results.json')
for name, src in (('quit-ab', ROOT / 'ab-quit'), ('rejected-startup-sync', ROOT / 'ab-startsync')):
    (art / name).mkdir(exist_ok=True)
    shutil.copy2(src / 'results.json', art / name / 'results.json')
for name, src in (('fps-check', FPS), ('rejected-startup-sync-fps', REJECT_FPS)):
    (art / name).mkdir(exist_ok=True)
    shutil.copy2(src / 'results.json', art / name / 'results.json')
(art / 'texture-verification').mkdir(exist_ok=True)
shutil.copy2(TEXV / 'verification.json', art / 'texture-verification' / 'verification.json')
for label in ('baseline', 'candidate'):
    shutil.copytree(TEXV / label / 'logs', art / 'texture-verification' / label / 'logs', dirs_exist_ok=True)
for f in ('compare.py', 'firstframe.py', 'publish_final.py', 'rejected-loadsync-and-probes.patch'):
    shutil.copy2(ROOT / f, art / f)
shutil.copy2(REPO / '.codex-build/map-load-20261005/measure_initial.py', art / 'measure_initial.py')
for t in tests:
    shutil.copy2(REPO / 'Misc/stress' / t, art / t)
patch = subprocess.check_output(['git', 'diff', '--', 'Quake'], cwd=REPO)
(art / 'source.patch').write_bytes(patch)
exe = ROOT / 'bin-final' / 'quakespasm.exe'
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
with zipfile.ZipFile(art / 'qssm-fast-loading-build.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in (ROOT / 'bin-final').iterdir():
        if p.suffix.lower() in ('.exe', '.dll'):
            z.write(p, p.name)
    z.writestr('source.patch', patch)
    z.writestr('README.txt', 'QSS-M fast-loading test build, based on ' + head +
               '\nUse your own Quake assets. Measurements and limits are in the HTML report.\n')

data = {
    'created': '2026-10-06', 'baseline_commit': head,
    'candidate_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
    'sources': {k: {x: v for x, v in s.items() if x != 'exe'} for k, s in raw['sources'].items()}, 'endpoint': raw['endpoint'], 'summary': summary, 'steps': steps,
    'fps_check': fps.get('summary', fps), 'quit_check': quit_ab['summary'],
    'rejected_startup_sync': {'lifecycle': reject_ab['summary'], 'fps': reject_fps.get('summary', reject_fps)},
    'texture_verification': texv, 'tests': tests,
    'limits': 'Windows 11, RTX 4070 (driver 610.88), windowed 800x600, VSync/MSAA off, start then e1m1 on a local listen server. '
              'Fresh process per sample, one retained warmup per engine, 9 measured rounds in rotating order. OS and driver caches warm. '
              'Signon 4 endpoint for every engine; a first presented in-game frame probe was used only for QSS-M diagnostics. '
              'QSS, FTEQW and ezQuake were not rerun; their original medians (166-1106 ms initial, 221-1009 ms change) are far behind.',
}
assert data['candidate_sha256'] == raw['sources']['candidate']['sha256'], 'packaged binary must be the measured one'

med = lambda k, l: summary[k][l]['median']
initial_before, initial_after = med('initial', 'baseline'), med('initial', 'candidate')
change_before, change_after = med('change', 'baseline'), med('change', 'candidate')
runner = lambda k: min((l for l in LABELS if l not in ('baseline', 'candidate')), key=lambda l: med(k, l))
fps_rows = ''.join(f'<tr><th scope="row">{m}</th><td>{fps["summary"]["grid"][m]:,.0f}</td><td>{fps["summary"]["final"][m]:,.0f}</td></tr>'
                   for m in fps['summary']['final'])
rej = reject_fps['summary']
rej_rows = ''.join(f'<tr><th scope="row">{m}</th><td>{rej["grid"][m]:,.0f}</td><td>{rej["loadsync"][m]:,.0f}</td><td>{(rej["loadsync"][m]/rej["grid"][m]-1)*100:+.1f}%</td></tr>' for m in rej['grid'])
embedded = json.dumps(data).replace('</', '<\\/')

panel = f'''<!-- LOADING_OPTIMIZATION_BEGIN -->
<details open id="loading-patch"><summary>Tested source changes · fastest map loading</summary><div class="inside">
<div class="eyebrow">Initial map load and map change / source A/B / October 6, 2026</div>
<h3 class="lo-headline">QSS-M now loads maps faster than every released engine tested.</h3>
<div class="summary lo-summary">
<div class="stat"><div class="kicker">Initial map load · start</div><div class="big q">{initial_before:.0f} → {initial_after:.0f} ms</div><p>#1. Next: {PRETTY[runner('initial')]} at {med('initial', runner('initial')):.0f} ms. Median of nine.</p></div>
<div class="stat"><div class="kicker">Map change · e1m1</div><div class="big q">{change_before:.0f} → {change_after:.0f} ms</div><p>#1 by about 3×. Next: {PRETTY[runner('change')]} at {med('change', runner('change')):.0f} ms.</p></div>
<div class="stat"><div class="kicker">Held steady</div><div class="big">FPS · quit · textures</div><p>Timedemo FPS within +2–4%, quit {quit_ab['summary']['final']['quit']:.0f} vs {quit_ab['summary']['baseline']['quit']:.0f} ms, 1,700 texture uploads identical.</p></div>
</div>
<div class="toolbar loading-toolbar"><div class="segmented" role="group" aria-label="Choose loading comparison"><button id="load-initial" aria-pressed="true">Initial load · start</button><button id="load-change" aria-pressed="false">Map change · e1m1</button></div><button class="icon-btn" id="loading-export">Raw results ↓</button><a class="download" href="loading-optimization/final-2026-10-06/qssm-fast-loading-build.zip" download>Test build + patch ↓</a></div>
<div class="chart"><div class="fps-chart-heading"><h3 id="load-title">Initial map load</h3><span class="soft">Lower is faster · bar = median, dots = each of nine runs</span></div><div id="load-bars" aria-live="polite"></div></div>
<p class="chart-note">Fresh process per run, one retained warmup per engine, nine measured rounds in rotating engine order. Windowed 800 × 600, VSync/MSAA off, sound on, same assets and controls as the six-engine lifecycle test. Endpoint for every engine: the native signon-4 notification after the <code>map</code> command. QSS, FTEQW and ezQuake were not rerun; their original medians are 166–1,106 ms (initial) and 221–1,009 ms (change).</p>
<h3 class="lo-sub">What changed</h3>
<p class="lo-lede">Seven changes, each kept only after its own counterbalanced A/B. Rows show that A/B's medians, so they are not additive.</p>
<ol class="lo-steps" id="lo-steps"></ol>
<details><summary>All nine samples per engine</summary><div class="inside"><div class="table-wrap"><table><caption>Final head-to-head · milliseconds · median [minimum–maximum]</caption><thead><tr><th>Engine/build</th><th>Startup</th><th>Initial load</th><th>Map change</th><th>Quit</th></tr></thead><tbody id="load-samples"></tbody></table></div><div class="table-wrap"><table class="lo-raw"><caption>Every run · initial / change · ms</caption><tbody id="load-raw"></tbody></table></div></div></details>
<details><summary>Checks: FPS, quit, textures, tests</summary><div class="inside">
<p><strong>Gameplay FPS.</strong> Native timedemos, two warmups then three measured passes per map, ABBA process order, 800 × 600. Previous build vs final:</p>
<div class="table-wrap"><table><thead><tr><th>Demo</th><th>Before (FPS)</th><th>Final (FPS)</th></tr></thead><tbody>{fps_rows}</tbody></table></div>
<p><strong>Quit.</strong> Nine-run A/B against the original build: {quit_ab['summary']['baseline']['quit']:.1f} ms before, {quit_ab['summary']['final']['quit']:.1f} ms after.</p>
<p><strong>Textures.</strong> With <code>tex_verify 1</code>, all {texv['maps']['start']['lines']} start and {texv['maps']['e1m1']['lines']} e1m1 mip upload records match the original build byte for byte (CPU upload data and flags, not GPU readback). Hashes equal this morning's verification.</p>
<p><strong>Tests.</strong> {len(tests)} compiled regression tests pass, including new ones for load-scoped directory caching and abort, pooled texture names with refill/reset, and sound reuse with recycling instead of "out of sfx_t".</p>
</div></details>
<details><summary>Tried and rejected</summary><div class="inside">
<p><strong>Synchronous GL debug output from startup.</strong> It removed NVIDIA's first-load stall (initial {reject_ab['summary']['sync']['initial']:.0f} vs {reject_ab['summary']['nosync']['initial']:.0f} ms) but keeps the driver single-threaded all session, costing gameplay FPS and map-change time. Not shipped; the patch is kept for reference.</p>
<div class="table-wrap"><table><thead><tr><th>Demo</th><th>Threaded (FPS)</th><th>Sync debug (FPS)</th><th>Change</th></tr></thead><tbody>{rej_rows}</tbody></table></div>
<p>Also measured and dropped: sync only during the first load (slower on both), dummy texture or GL-call warm-ups at startup (no effect), serial texture preparation (slower), longer idle before the first map (no effect), and the earlier sky, storage, flush and mesh-storage probes.</p>
</div></details>
<details><summary>Limits</summary><div class="inside"><p>{data['limits']}</p><p>Raw logs, per-step results, scripts, the source patch and tests are in <code>loading-optimization/final-2026-10-06/</code>. Source is not committed yet.</p></div></details>
</div></details>
<style>.lo-headline{{font-size:24px;letter-spacing:-.03em;margin:0 0 20px;font-weight:600;color:var(--text)}}.lo-summary{{grid-template-columns:repeat(3,1fr)}}.lo-sub{{font-size:17px;font-weight:600;color:var(--text);margin:34px 0 6px}}.lo-lede{{font-size:13px;color:var(--muted)}}.loading-toolbar{{margin:20px 0}}.load-row{{display:grid;grid-template-columns:170px minmax(80px,1fr) 120px;gap:18px;align-items:center;padding:14px 0}}.load-row strong{{color:var(--text);font-weight:550}}.load-track{{height:14px;background:var(--card);border-radius:20px;position:relative}}.load-fill{{height:100%;background:var(--load-color);border-radius:20px;opacity:.85;transition:width .5s cubic-bezier(.2,.8,.2,1)}}.load-dot{{position:absolute;top:50%;width:5px;height:5px;margin:-2.5px 0 0 -2.5px;border-radius:50%;background:var(--text);opacity:.55;transition:left .5s cubic-bezier(.2,.8,.2,1)}}.load-value{{text-align:right;font-variant-numeric:tabular-nums;color:var(--text)}}.load-value small{{display:block;color:var(--muted);font-size:11px}}.lo-steps{{list-style:none;padding:0;margin:14px 0 22px;display:grid;gap:10px;counter-reset:lo}}.lo-step{{counter-increment:lo;display:grid;grid-template-columns:34px minmax(0,1fr) 230px;gap:16px;padding:16px 18px;border:1px solid var(--line);border-radius:14px;background:var(--bg);transition:border-color .2s}}.lo-step:hover{{border-color:var(--q)}}.lo-step:before{{content:counter(lo);display:grid;place-items:center;width:28px;height:28px;border-radius:9px;background:var(--card);color:var(--q);font-weight:700;font-size:12px}}.lo-step h4{{margin:2px 0 4px;color:var(--text);font-size:14px;font-weight:600;letter-spacing:0;text-transform:none}}.lo-step p{{margin:0;font-size:12.5px}}.lo-step code{{font-size:11px}}.lo-delta{{display:grid;gap:8px;align-content:start;font-size:11px;font-variant-numeric:tabular-nums}}.lo-delta div{{display:grid;grid-template-columns:52px 1fr;gap:8px;align-items:center}}.lo-pair{{position:relative;height:8px;background:var(--card);border-radius:8px}}.lo-pair i{{position:absolute;left:0;top:0;height:100%;border-radius:8px;transition:width .5s cubic-bezier(.2,.8,.2,1)}}.lo-pair i.b{{background:color-mix(in srgb,var(--muted) 45%,transparent)}}.lo-pair i.a{{background:var(--q)}}.lo-delta small{{grid-column:2;color:var(--muted);margin-top:-5px}}.lo-raw td{{text-align:right}}@media(max-width:800px){{.lo-summary{{grid-template-columns:1fr}}.lo-step{{grid-template-columns:30px minmax(0,1fr)}}.lo-delta{{grid-column:1 / -1}}}}@media(max-width:650px){{.load-row{{grid-template-columns:1fr 100px;gap:8px}}.load-track{{grid-row:2;grid-column:1 / -1}}.load-value{{grid-column:2;grid-row:1}}}}@media(prefers-reduced-motion:reduce){{.load-fill,.load-dot,.lo-pair i{{transition:none}}}}</style>
<script type="application/json" id="loading-data">{embedded}</script>
<script>(()=>{{const data=JSON.parse(document.getElementById('loading-data').textContent),names={{baseline:'QSS-M · before',candidate:'QSS-M · now',ironwail:'Ironwail 0.8.2',vkquake:'vkQuake 1.36.0'}},colors={{baseline:'#9daebc',candidate:'var(--q)',ironwail:'#70c6ff',vkquake:'#f08bbb'}},labels=Object.keys(names),bars=document.getElementById('load-bars'),esc=s=>String(s).replace(/[&<>"]/g,c=>({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}}[c]));
for(const label of labels){{const row=document.createElement('div');row.className='load-row';row.style.setProperty('--load-color',colors[label]);row.innerHTML='<strong>'+names[label]+'</strong><div class="load-track"><div class="load-fill"></div>'+data.summary.initial[label].samples.map(()=>'<span class="load-dot"></span>').join('')+'</div><div class="load-value"></div>';row.dataset.engine=label;bars.appendChild(row);}}
function render(key){{document.getElementById('load-title').textContent=key==='initial'?'Initial map load · start':'Map change · e1m1';for(const k of ['initial','change'])document.getElementById('load-'+k).setAttribute('aria-pressed',String(key===k));const values=data.summary[key],max=Math.max(...labels.map(l=>values[l].max))*1.05;for(const row of bars.children){{const v=values[row.dataset.engine];row.querySelector('.load-fill').style.width=(v.median/max*100)+'%';row.querySelectorAll('.load-dot').forEach((d,i)=>d.style.left=(v.samples[i]/max*100)+'%');row.querySelector('.load-value').innerHTML=Math.round(v.median)+' ms<small>'+Math.round(v.min)+'–'+Math.round(v.max)+' ms</small>';}}}}
for(const key of ['initial','change'])document.getElementById('load-'+key).onclick=()=>render(key);
const stepMax=Math.max(...data.steps.flatMap(s=>[...s.initial,...s.change]));
for(const s of data.steps){{const li=document.createElement('li');li.className='lo-step';const pair=(lab,v)=>'<div><span>'+lab+'</span><span class="lo-pair"><i class="b" style="width:'+(v[0]/stepMax*100)+'%"></i><i class="a" style="width:'+(v[1]/stepMax*100)+'%"></i></span><small>'+v[0].toFixed(0)+' → '+v[1].toFixed(0)+' ms</small></div>';li.innerHTML='<div><h4>'+esc(s.title)+'</h4><p>'+esc(s.body)+'</p><p class="soft"><code>'+esc(s.files)+'</code> · '+s.samples+' runs each'+(s.note?' · '+esc(s.note):'')+(s.first_frame?' · first frame '+s.first_frame.initial[0].toFixed(0)+' → '+s.first_frame.initial[1].toFixed(0)+' ms (initial), '+s.first_frame.change[0].toFixed(0)+' → '+s.first_frame.change[1].toFixed(0)+' ms (change)':'')+'</p></div><div class="lo-delta">'+pair('Initial',s.initial)+pair('Change',s.change)+'</div>';document.getElementById('lo-steps').appendChild(li);}}
document.getElementById('loading-export').onclick=()=>{{const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{{type:'application/json'}})),a=document.createElement('a');a.href=url;a.download='qssm-loading-final-results.json';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}};
for(const label of labels){{const tr=document.createElement('tr');tr.innerHTML='<th scope="row">'+names[label]+'</th>'+['startup','initial','change','quit'].map(k=>{{const v=data.summary[k][label];return '<td>'+v.median.toFixed(1)+' ['+v.min.toFixed(1)+'–'+v.max.toFixed(1)+']</td>';}}).join('');document.getElementById('load-samples').appendChild(tr);
const r=document.createElement('tr');r.innerHTML='<th scope="row">'+names[label]+'</th>'+data.summary.initial[label].samples.map((v,i)=>'<td>'+Math.round(v)+' / '+Math.round(data.summary.change[label].samples[i])+'</td>').join('');document.getElementById('load-raw').appendChild(r);}}
render('initial');}})();</script>
<!-- LOADING_OPTIMIZATION_END -->
'''

target = OUT / 'index.html'
text = target.read_text(encoding='utf-8')
original = {m[1]: m[2] for m in re.finditer(r'<script[^>]*id="([^"]+)"[^>]*type="application/json"[^>]*>(.*?)</script>|<script[^>]*type="application/json"[^>]*id="([^"]+)"[^>]*>(.*?)</script>', text, re.S) if m[1]}
assert '<!-- LOADING_OPTIMIZATION_BEGIN -->' in text
text = re.sub(r'<!-- LOADING_OPTIMIZATION_BEGIN -->.*?<!-- LOADING_OPTIMIZATION_END -->\s*', lambda _: panel, text, flags=re.S)
text = text.replace('Tested QSS-M loading improvement →', 'QSS-M loading: now fastest →')
target.write_text(text, encoding='utf-8')
print(json.dumps({k: {l: round(summary[k][l]['median'], 1) for l in LABELS} for k in ('initial', 'change')}, indent=1))
print('steps', [(s['title'][:30], [round(x) for x in s['initial']], [round(x) for x in s['change']]) for s in steps])
