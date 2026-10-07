import json,statistics,shutil,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parent
OUT=ROOT.parents[1]/'Misc/performance/engine-comparison-2026-10-05'
rows=json.loads((ROOT/'diagnostic/repeat-results.json').read_text())
labels={'baseline_repeat':'800 × 600 · renderer defaults','fastsky_repeat':'800 × 600 · main sky/flame settings','scene_off_repeat':'800 × 600 · scene cache off','4k_repeat':'3840 × 2160 · renderer defaults','4k_fastsky_repeat':'3840 × 2160 · main sky/flame settings','menu_settle_repeat':'800 × 600 · menu settle before first pass'}
assert len(rows)==6 and all(len(r['fps'])==8 and set(r['frames'])=={1445} for r in rows)
for r in rows:
 expected={'width':3840,'height':2160} if r['variant'].startswith('4k') else {'width':800,'height':600}
 assert r['geometry']==expected
 r['first_fps']=r['fps'][0];r['repeat_median_fps']=statistics.median(r['fps'][1:])
sha=hashlib.sha256((ROOT/'diagnostic/qssm/QSS-M-w64.exe').read_bytes()).hexdigest()
assert sha=='5cacdcc77bef96b045fb6beca1ad80272f3cdef7315119774dd87a84f84a7ae4'
dest=OUT/'fps/diagnostics';dest.mkdir(parents=True,exist_ok=True)
for r in rows:shutil.copy2(ROOT/f'diagnostic/{r["variant"]}.log',dest/f'{r["variant"]}.log')
shutil.copy2(ROOT/'diagnostic/baseline-0.log',dest/'settings-query.log')
for script in ['diagnose_repeat.py','publish_diagnosis.py']:shutil.copy2(ROOT/script,dest/script)
data={'purpose':'Supplemental QSS-M diagnostics; excluded from the six-engine ranking. Same executable and Aerowalk demo as baseline. Eight timedemos per process; repeat median excludes first pass. One session per condition, sequential order; no statistical significance claimed.','exe_sha256':sha,'baseline_cvars':{'r_shadows':'0','r_outline':'0','r_scenecache':'auto (empty string)','r_bmodelcache':'1','r_aliaslightcache':'1','host_maxfps':'0','vid_vsync':'0','vid_fsaa':'0'},'main_settings_subset':{'r_fastsky':'2','r_waterwarp':'0','r_drawflame':'0','r_shadows':'.125','r_outline':'5'},'samples':rows}
(dest/'results.json').write_text(json.dumps(data,indent=2))
table=''.join(f'<tr><td>{labels[r["variant"]]}</td><td>{r["first_fps"]:,.0f}</td><td>{r["repeat_median_fps"]:,.0f}</td><td><a href="fps/diagnostics/{r["variant"]}.log">Native log</a></td></tr>' for r in rows)
note='''<details open id="fps-diagnostics"><summary>Why your live QSS-M FPS can be higher</summary><div class="inside"><p><strong>The chart measures first timedemos in fresh processes, not warmed live gameplay.</strong> Its discarded warm-up runs in a separate process. It warms OS/driver caches, but cannot preserve engine state for the measured passes. On the same PC and identical QSS-M executable, Aerowalk rose from <strong>2,102 FPS on the first pass to 2,840 FPS at the repeat-pass median</strong> in one process.</p><p>The benchmark's native settings query confirms <code>r_shadows 0</code> and <code>r_outline 0</code>, with scene caching on its automatic default. The first-pass penalty also appears with scene caching disabled, so scene-cache construction alone does not explain it. Demo reloads reset scene caches. These checks demonstrate a warm-process effect; they do not identify a specific driver, shader, or CPU-cache cost.</p><div class="table-wrap"><table><caption>QSS-M only · Aerowalk · FPS · eight passes in each process</caption><thead><tr><th>Diagnostic condition</th><th>First pass</th><th>Median of passes 2–8</th><th>Evidence</th></tr></thead><tbody>'''+table+'''</tbody></table></div><p>The saved main config uses <code>r_fastsky 2</code>, <code>r_waterwarp 0</code>, and <code>r_drawflame 0</code>. Applying those three together produced no clear uplift in this sweep. Its outlines and shadows are enabled, unlike the benchmark. Both 4K conditions had a verified 3840 × 2160 client area.</p><p>Live FPS counts rendered frames over about 0.75 seconds. Timedemo processes the recorded network messages at accelerated speed, with this demo advancing one packet per rendered frame after signon. The turning view and demo-decoding workload differ from live movement, so 2,500+ live FPS is compatible with a lower timedemo result. Conditions here ran sequentially with one session each; ranges overlap and this is not a controlled estimate of each setting's isolated cost. Supplemental QSS-M results are excluded from the six-engine ranking. A warmed-game ranking would require the same revised procedure for every engine.</p><div class="artifact-links"><a href="fps/diagnostics/results.json">Diagnostic results & settings</a><a href="fps/diagnostics/settings-query.log">Actual benchmark cvars</a></div></div></details>
'''
for path in [ROOT/'fps-section.html',OUT/'index.html']:
 text=path.read_text(encoding='utf-8')
 assert 'id="fps-diagnostics"' not in text
 text=text.replace('<details class="fps-matrix" open>',note+'<details class="fps-matrix" open>',1)
 text=text.replace('Five measured passes after one discarded warm-up. Select a map to compare.','Five fresh-process timedemos per map. OS caches warmed; engine state resets each pass. Select a map to compare.')
 path.write_text(text,encoding='utf-8')
with (OUT/'README.md').open('a',encoding='utf-8') as f:f.write('\n\n## QSS-M live FPS diagnostic\n\nThe FPS section now includes supplemental same-process Aerowalk repeats, verified 4K checks, native cvar queries, and a comparison with the saved main config’s sky settings. See fps/diagnostics/results.json and its native logs. These 48 passes are excluded from the original 324-pass ranking. First-process timedemos do not predict warmed live-game FPS; scene-cache-disabled repeats retain the first-pass penalty. The exact internal warm-up cost has not been isolated.\n')
print(json.dumps([{ 'condition':r['variant'],'first':r['first_fps'],'repeat_median':r['repeat_median_fps']} for r in rows],indent=2))
