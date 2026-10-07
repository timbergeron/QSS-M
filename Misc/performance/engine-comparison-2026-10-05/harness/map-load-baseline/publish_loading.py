"""Append a measured source A/B panel; preserve every original benchmark dataset."""
import hashlib, html, json, re, shutil, statistics, subprocess, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
OUT = REPO / 'Misc/performance/engine-comparison-2026-10-05'
BATCH = ROOT / 'final-ab'
raw = json.loads((BATCH / 'results.json').read_text())
assert len(raw['results']) == 20 and len(raw['warmups']) == 4
assert all(r['valid'] for r in raw['results'] + raw['warmups'])
artifact = OUT / 'loading-optimization'
artifact.mkdir(exist_ok=True)
summary = {}
for key in ('startup','initial','change','quit'):
    summary[key] = {}
    for label in ('baseline','candidate','ironwail','vkquake'):
        values = [r['metrics_ms'][key] for r in raw['results'] if r['label'] == label]
        assert len(values) == 5
        summary[key][label] = {'median':statistics.median(values),'min':min(values),'max':max(values),'samples':values}
stages = {}
for label in ('baseline','candidate'):
    values = []
    for row in raw['results']:
        if row['label'] != label:
            continue
        text = (BATCH / row['log']).read_text().split('BENCH_BEGIN_change')[0]
        match = re.search(r'Mod_LoadTextures maps/start.bsp: findfile ([\d.]+)ms \((\d+) calls\).*imageload ([\d.]+)ms \((\d+) calls\)',text)
        commit = re.search(r'TexMgr prepare maps/start.bsp:.*commit ([\d.]+)ms',text)
        assert match and commit
        values.append({'findfile_ms':float(match[1]),'findfile_calls':int(match[2]),
                       'image_ms':float(match[3]),'image_calls':int(match[4]),'commit_ms':float(commit[1])})
    stages[label] = {key:statistics.median(v[key] for v in values) for key in values[0]}
raw.update(summary=summary,stages=stages,
           baseline_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
           source_patch='source.patch',texture_verification=json.loads((ROOT/'texture-verification/verification.json').read_text()),
           validation='Release x64 MSVC build; candidate file-search regression, observer HUD cache, startup shaders, and external brush cache tests passed; read-only peer review approved.',
           sources_researched={'ironwail':'https://github.com/andrei-drexler/ironwail/blob/v0.8.2/Quake/image.c#L112-L160',
                               'vkquake':'https://github.com/Novum/vkQuake/blob/1.36.0/Quake/image.c#L156-L190'},
           limits='Windowed 800x600, start -> e1m1, five fresh processes per engine after one explicit warmup. OS/driver caches warm. Initial endpoint is signon notification, not a first-visible-frame sensor. No work deferred past this endpoint by the patch. These results do not measure FPS or remote connects.')
for label in ('baseline','candidate','ironwail','vkquake'):
    shutil.copytree(BATCH/label/'logs',artifact/label/'logs',dirs_exist_ok=True)
shutil.copytree(ROOT/'texture-verification',artifact/'texture-verification',ignore=shutil.ignore_patterns('quakespasm.exe','*.dll','*.pak','*.pdb','profile','id1','qw','stdout.log'),dirs_exist_ok=True)
patch = subprocess.check_output(['git','diff','--','Quake/common.c','Quake/common.h','Quake/image.c'],cwd=REPO)
(artifact/'source.patch').write_bytes(patch)
shutil.copy2(REPO/'Misc/stress/test_image_file_candidates.py',artifact/'test_image_file_candidates.py')
shutil.copy2(ROOT/'measure_initial.py',artifact/'measure_initial.py')
(artifact/'results.json').write_text(json.dumps(raw,indent=2),encoding='utf-8')
with zipfile.ZipFile(artifact/'qssm-loading-test-build.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in (ROOT/'bin-candidates').iterdir():
        if p.suffix.lower() in ('.exe','.dll'):
            z.write(p,p.name)
    z.writestr('source.patch',patch)
    z.write(REPO/'Misc/stress/test_image_file_candidates.py','test_image_file_candidates.py')
    z.writestr('README.txt','QSS-M experimental loading build, based on '+raw['baseline_commit']+'\nUse your own Quake assets. Source remains uncommitted. Native measurements and limits are in the HTML report.\n')
before, after = summary['initial']['baseline']['median'],summary['initial']['candidate']['median']
improvement = (1-after/before)*100
change_gain = (1-summary['change']['candidate']['median']/summary['change']['baseline']['median'])*100
embedded = json.dumps(raw).replace('</','<\\/')
panel = f'''<!-- LOADING_OPTIMIZATION_BEGIN -->
<details open id="loading-patch"><summary>Tested source change · faster replacement-image searches</summary><div class="inside">
<div class="eyebrow">Initial map loading / source A/B / October 6, 2026</div>
<h3>Less searching. The same texture data.</h3>
<div class="summary"><div class="stat"><div class="kicker">Initial map load · start</div><div class="big q">{before:.0f} → {after:.0f} ms</div><p>{improvement:.1f}% lower median time in the final matched-build comparison.</p></div>
<div class="stat"><div class="kicker">Map change · e1m1</div><div class="big q">{change_gain:.1f}% faster</div><p>Measured after start in the same fresh process.</p></div>
<div class="stat"><div class="kicker">Position against the releases</div><div class="big">Gap narrowed</div><p>Ironwail and vkQuake still lead this initial-load test.</p></div></div>
<p>QSS-M probes up to 19 paths for each optional replacement image. On start, all 166 image searches miss: 3,154 file lookups. <a href="{raw['sources_researched']['ironwail']}" target="_blank" rel="noopener">Ironwail 0.8.2</a> and <a href="{raw['sources_researched']['vkquake']}" target="_blank" rel="noopener">vkQuake 1.36.0</a> each try five image formats per basename. QSS-M's extra formats and fallback paths are retained.</p>
<p>The patch shares parent-directory checks within one image search. It still checks PACK entries, preserves format and mount precedence, and discards its directory cache before returning. Files added later remain discoverable.</p>
<div class="toolbar loading-toolbar"><div class="segmented" role="group" aria-label="Choose loading comparison"><button id="load-initial" aria-pressed="true">Initial load · start</button><button id="load-change" aria-pressed="false">Map change · e1m1</button></div><button class="icon-btn" id="loading-export">Raw results ↓</button><a class="download" href="loading-optimization/qssm-loading-test-build.zip" download>Test build + patch ↓</a></div>
<div class="chart"><div class="fps-chart-heading"><h3 id="load-title">Initial map load</h3><span class="soft">Lower is faster · median of five</span></div><div id="load-bars" aria-live="polite"></div></div>
<p class="chart-note">Fresh process per run; one retained warmup per engine, then five measured rounds in rotating order. Windowed 800 × 600, VSync/MSAA off, sound enabled, original assets and controls. Same MSVC Release build settings for QSS-M before/after. OS and driver caches are warm. The endpoint is the native <code>CL_SignonReply: 4</code> notification; this patch does not move loading work beyond it.</p>
<details><summary>All timings and the remaining bottleneck</summary><div class="inside"><div class="table-wrap"><table><caption>Final source A/B · milliseconds · median [minimum–maximum]</caption><thead><tr><th>Engine/build</th><th>Startup</th><th>Initial load</th><th>Map change</th><th>Quit</th></tr></thead><tbody id="load-samples"></tbody></table></div>
<p>World-texture filesystem lookup time fell from {stages['baseline']['findfile_ms']:.1f} to {stages['candidate']['findfile_ms']:.1f} ms. These stage timers overlap with image loading and must not be added together. Prepared GPU texture commits measured {stages['baseline']['commit_ms']:.1f} ms before and {stages['candidate']['commit_ms']:.1f} ms after. We have not isolated why that interval increased; it includes driver waits. Upload batching is the next profiling target. vkQuake uses parallel texture jobs and Vulkan staging; that implementation cannot be transplanted directly into QSS-M's OpenGL renderer.</p>
<p>Validation: four focused regression checks and both Release builds passed; peer review approved. The diagnostic runs matched 1,700 ordered map/model mipmap records and the final UI atlas. This validates CPU upload data and flags, not GPU readback. Intermediate UI atlas upload frequency differed during startup. Diagnostic runs are excluded from timing statistics.</p>
<p>These results cover start and e1m1 loading. They establish no FPS or public-server connection gain. The original six-engine dataset remains unchanged; this section reports an experimental source build.</p></div></details>
</div></details>
<style>.loading-toolbar{{margin:20px 0}}.load-row{{display:grid;grid-template-columns:180px minmax(80px,1fr) 150px;gap:18px;align-items:center;padding:16px 0}}.load-track{{height:12px;background:var(--card);border-radius:20px}}.load-fill{{height:100%;background:var(--load-color);border-radius:20px;transition:width .45s ease}}.load-value{{text-align:right;font-variant-numeric:tabular-nums}}.load-value small{{display:block;color:var(--muted);font-size:11px}}@media(max-width:650px){{.load-row{{grid-template-columns:1fr 100px;gap:8px}}.load-track{{grid-row:2;grid-column:1 / -1}}.load-value{{grid-column:2;grid-row:1}}}}@media(prefers-reduced-motion:reduce){{.load-fill{{transition:none}}}}</style>
<script type="application/json" id="loading-data">{embedded}</script>
<script>(()=>{{const data=JSON.parse(document.getElementById('loading-data').textContent),names={{baseline:'QSS-M · before',candidate:'QSS-M · patched',ironwail:'Ironwail 0.8.2',vkquake:'vkQuake 1.36.0'}},colors={{baseline:'#9daebc',candidate:'var(--q)',ironwail:'#70c6ff',vkquake:'#f08bbb'}};const labels=Object.keys(names),bars=document.getElementById('load-bars');
for(const label of labels){{const row=document.createElement('div');row.className='load-row';row.style.setProperty('--load-color',colors[label]);row.innerHTML='<strong>'+names[label]+'</strong><div class="load-track"><div class="load-fill"></div></div><div class="load-value"></div>';row.dataset.engine=label;bars.appendChild(row);}}
function render(key){{document.getElementById('load-title').textContent=key==='initial'?'Initial map load · start':'Map change · e1m1';for(const k of ['initial','change'])document.getElementById('load-'+k).setAttribute('aria-pressed',String(key===k));const values=data.summary[key],max=Math.max(...labels.map(l=>values[l].max))*1.05;for(const row of bars.children){{const v=values[row.dataset.engine];row.querySelector('.load-fill').style.width=(v.median/max*100)+'%';row.querySelector('.load-value').innerHTML=Math.round(v.median)+' ms<small>'+Math.round(v.min)+'–'+Math.round(v.max)+' ms</small>';}}}}
for(const key of ['initial','change'])document.getElementById('load-'+key).onclick=()=>render(key);
document.getElementById('loading-export').onclick=()=>{{const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{{type:'application/json'}})),a=document.createElement('a');a.href=url;a.download='qssm-loading-optimization-results.json';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}};
for(const label of labels){{const tr=document.createElement('tr');tr.innerHTML='<th scope="row">'+names[label]+'</th>'+['startup','initial','change','quit'].map(k=>{{const v=data.summary[k][label];return '<td>'+v.median.toFixed(1)+' ['+v.min.toFixed(1)+'–'+v.max.toFixed(1)+']</td>';}}).join('');document.getElementById('load-samples').appendChild(tr);}}render('initial');}})();</script>
<!-- LOADING_OPTIMIZATION_END -->
'''
target = OUT / 'index.html'
text = target.read_text(encoding='utf-8')
original_data = {}
for attrs,content in re.findall(r'<script([^>]*)>(.*?)</script>',text,re.S):
    key = re.search(r'id="([^"]+)"',attrs)
    if 'application/json' in attrs and key and key[1]!='loading-data':
        original_data[key[1]] = content
if '<!-- LOADING_OPTIMIZATION_BEGIN -->' in text:
    text = re.sub(r'<!-- LOADING_OPTIMIZATION_BEGIN -->.*?<!-- LOADING_OPTIMIZATION_END -->\s*',lambda _:panel,text,flags=re.S)
else:
    text = text.replace('<details open id="fps-patch">',panel+'\n<details open id="fps-patch">',1)
    text = text.replace('<p class="chart-note" id="life-note"></p>','<p class="chart-note" id="life-note"></p><a class="download" href="#loading-patch">Tested QSS-M loading improvement →</a>',1)
for key,content in original_data.items():
    assert re.search(r'<script[^>]*id="'+key+r'"[^>]*>(.*?)</script>',text,re.S)[1]==content
target.write_text(text,encoding='utf-8')
(ROOT/'loading-section.html').write_text(panel,encoding='utf-8')
print(json.dumps({'summary':summary,'stages':stages,'initial_improvement_percent':improvement,'map_change_improvement_percent':change_gain},indent=2))
