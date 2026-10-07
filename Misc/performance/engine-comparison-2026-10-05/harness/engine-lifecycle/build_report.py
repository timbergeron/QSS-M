"""Build the six-engine lifecycle dashboard while preserving the verified FPS data."""
import csv, hashlib, html, json, re, shutil, statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent
OUT=ROOT.parents[1]/'Misc/performance/engine-comparison-2026-10-05'
DEFS=[
 ('startup','Engine start','Start','Process creation to the native BENCH_READY command-readiness marker, with no map loaded.','lifecycle'),
 ('launch_map','Launch → first map','Launch + map','A separate fresh process loading start immediately; process creation to completed client signon. This measures the combined path directly.','launch_map'),
 ('initial','Initial map load','First map','The first map start command in an already-started fresh process, immediately before the command to completed client signon. Startup and menu settling are excluded.','lifecycle'),
 ('change','Subsequent map load','Map change','map e1m1 after start has completed signon in the same process; immediate command marker to completed client signon. Common assets are already cached.','lifecycle'),
 ('connect','Connect · local server','Connect · local','A fresh client settles at the menu, then joins e1m1 on loopback. Immediate connect-command marker to completed client signon. Five NetQuake clients use one QSS-M server; ezQuake uses an FTE QuakeWorld server.','connect'),
 ('connect_remote','Connect · Denver','Connect · Denver','Fresh client joining denver.quakeone.com:26000 on aerowalk. Immediate hostname connect-command marker to native signon stage 4. Same preinstalled map and custom sounds; server empty before each attempt. ezQuake cannot join this NetQuake server.','connect_remote'),
 ('quit','Engine quit','Quit','The marker immediately before native quit to observed process exit, after e1m1 is loaded locally. Connection-session quit is excluded. Native console quit bypasses confirmation menus.','lifecycle')]

def main():
 raw=json.loads((ROOT/'results.json').read_text());eng=raw['engines'];keys=list(eng)
 assert len(raw['results'])==90 and all(r['valid'] and r['exit_code']==0 for r in raw['results'])
 assert len({(r['engine'],r['run'],r['mode']) for r in raw['results']})==90
 remote=json.loads((ROOT/'remote/results.json').read_text())
 assert len(remote['results'])==25 and all(r['valid'] and 'connect_remote' in r['metrics_ms'] for r in remote['results'])
 raw['public_server']={k:v for k,v in remote.items() if k not in ('results','excluded_results')}
 for r in remote.get('excluded_results',[]):
  r['log']='remote/'+r['log'].replace('\\','/');raw.setdefault('excluded_results',[]).append(r)
 raw['public_server']['setup_logs']=['remote/'+str(p.relative_to(ROOT/'remote')).replace('\\','/') for p in sorted((ROOT/'remote/setup-logs').glob('*.log'))]
 raw['public_server']['diagnostic_logs']=['remote/'+str(p.relative_to(ROOT/'remote')).replace('\\','/') for p in sorted((ROOT/'remote/diagnostic-logs').glob('*.log'))]
 for r in remote['results']:
  r['log']='remote/'+r['log'].replace('\\','/');raw['results'].append(r)
 for r in raw['results']:
  log=(ROOT/r['log']).read_text(errors='replace')
  assert all(e['line'] in log for e in r['events'])
  assert r['window_pixels']=={'width':800,'height':600}
  assert all(v>0 for v in r['metrics_ms'].values())
 eng['qssm']['version']='1.6.9-7da539b · installed';eng['fteqw']['version']='git-30-f2576ed · installed'
 metrics=[]
 for key,title,short,definition,mode in DEFS:
  stats={}
  for e in keys:
   rows=sorted([r for r in raw['results'] if r['engine']==e and r['mode']==mode],key=lambda r:r['run'])
   assert [r['run'] for r in rows]==list(range(1,6)) or (mode=='connect_remote' and e=='ezquake' and not rows)
   samples=[r['metrics_ms'].get(key) for r in rows] if rows else [None]*5;valid=[v for v in samples if v is not None]
   stats[e]={'samples':samples,'n':len(valid),'median':statistics.median(valid) if valid else None,'min':min(valid) if valid else None,'max':max(valid) if valid else None}
  metrics.append({'key':key,'title':title,'short':short,'definition':definition,'mode':mode,'stats':stats})
 raw['verified']={'measured_launches':len(raw['results']),'normal_exits':sum(r['exit_code']==0 for r in raw['results']),'local_launches':90,'denver_launches':25,'timing_observations':sum(s['n'] for m in metrics for s in m['stats'].values())}
 missing=[eng[e]['name'] for e in keys if metrics[4]['stats'][e]['n']!=5]
 note='Same QSS-M NetQuake server for QSS-M, FTE, Ironwail, vkQuake, and QSS. ezQuake joins an FTE QuakeWorld server; its protocol and server workload differ, so no cross-protocol winner is claimed.'
 if missing:note+=' '+', '.join(missing)+' had no completed signon endpoint in five attempts; timing is unavailable.'
 data={'metrics':metrics,'connection_note':note,'remote_note':'Denver · aerowalk · denver.quakeone.com:26000. Five compatible NetQuake clients, same cached map/sounds, no players before each attempt, base protocol with extensions disabled where supported. Real network/server scheduling included. ezQuake requires QuakeWorld, so this test is not applicable.','raw':raw}
 old=(OUT/'index.html').read_text(encoding='utf-8')
 fpsblob=re.search(r'<script type="application/json" id="fps-data">(.*?)</script>',old,re.S).group(1)
 fps=json.loads(fpsblob);assert len(fps['raw']['samples'])==324
 fpssection=(ROOT.parent/'engine-fps-20261005/fps-section.html').read_text(encoding='utf-8').replace('__FPS_DATA__',fpsblob)
 for color,var in [('f6c680','ez'),('6ebeff','iron'),('f390ba','vk'),('7bd58d','qs')]:fpssection=fpssection.replace("'#"+color+"'","'var(--"+var+")'")
 fpssection=fpssection.replace('The original lifecycle section compares FTEQW and QSS-M; the four additional engines are included in FPS.','All six engines also receive the lifecycle tests above. Protocol and server differences for joining are described in the lifecycle methodology.')
 source=(ROOT.parent/'engine-comparison-20261005/report-template.html').read_text(encoding='utf-8')
 head=source.split('</head>')[0].replace('FTEQW vs QSS-M · Lifecycle benchmark','Quake Engine Lab · Six-engine lifecycle & FPS benchmarks')+'</head>'
 lifecycle=(ROOT/'lifecycle-section.html').read_text(encoding='utf-8').replace('__DATA__',json.dumps(data,separators=(',',':')).replace('</','<\\/'))
 def best(metric):
  m=next(m for m in metrics if m['key']==metric);k=min(keys,key=lambda k:m['stats'][k]['median']);return eng[k]['name'],m['stats'][k]['median']
 launchname,launch=best('launch_map');changename,change=best('change')
 summary=f'''<section class="summary" aria-label="Lifecycle findings"><div class="stat"><div class="kicker">Shortest launch → first map</div><div class="big q">{launchname}</div><p>{launch:,.0f} ms observed median · start</p></div><div class="stat"><div class="kicker">Shortest subsequent map load</div><div class="big">{changename}</div><p>{change:,.0f} ms observed median · start → e1m1</p></div><div class="stat"><div class="kicker">Lifecycle evidence</div><div class="big">{raw['verified']['timing_observations']} timings</div><p>115 fresh launches · 25 Denver joins · five rounds</p></div></section>'''
 definitions='<ul>'+''.join(f'<li><strong>{title}:</strong> {definition}</li>' for _,title,_,definition,_ in DEFS)+'</ul>'
 configs=''.join(f'<details><summary>{e["name"]} · {html.escape(e["version"])}</summary><div class="inside"><p>Executable SHA-256:</p><pre>{e["sha256"]}</pre><p>Native lifecycle settings:</p><pre>{html.escape(raw["config"][k])}</pre></div></details>' for k,e in eng.items())
 body=f'''<body><header><div class="wrap topbar"><a href="#top" class="brand"><span class="mark" aria-hidden="true">EL</span>ENGINE LAB <span class="soft">/ benchmarks</span></a><div class="top-actions"><a class="method-link" href="#lifecycle">Lifecycle</a><a class="method-link" href="#fps">Map FPS</a><a class="method-link" href="#method">Methodology</a><button class="icon-btn" id="theme" aria-label="Switch to light theme">Light</button><button class="icon-btn" id="export">Export all JSON ↓</button></div></div></header>
<main class="wrap" id="top"><section class="hero"><div class="eyebrow">Measured on Windows · October 5, 2026</div><h1>Quake engines<span>,<br>measured.</span></h1><p>Startup, quit, map loading, and local plus Denver server joining across six engines. Nine-map FPS results complete the view, with every measured run available to inspect.</p><div class="chips"><span class="chip"><strong>6 engines</strong> · all lifecycle tests</span><span class="chip"><strong>5 measured runs</strong> per test</span><span class="chip"><strong>9 FPS maps</strong></span><span class="chip">800 × 600 · sound on</span></div><p class="machine-line">AMD Ryzen 5 3600X · NVIDIA RTX 4070 · driver 591.86 · Windows 11 build 26100</p></section>{summary}{lifecycle}{fpssection}
<section class="method" id="method"><h2>How lifecycle time was measured</h2><div class="method-intro"><strong>Six engines, one local machine, five rotating rounds.</strong> All six lifecycle datasets were measured again under this setup. Earlier two-engine lifecycle results and pilot attempts are excluded from these summaries. The 324 existing FPS passes remain unchanged. FTE lifecycle sessions were repeated with its native -noupdates option after an update-source prompt was confirmed; prior FTE attempts are excluded.</div>
<details open><summary>Timing endpoints and comparable workloads</summary><div class="inside">{definitions}<p>Completed client signon is the native <code>CL_SignonReply: 4</code> event for NetQuake clients. ezQuake uses its native <code>f_spawn</code> alias executed after <code>CL_MakeActive</code>; this includes its shader-program preparation and the next command-buffer service. These are observable client-readiness milestones, not measurements of the first displayed pixel.</p><p>{html.escape(note)}</p></div></details>
<details><summary>Sessions, caches, settings, and scheduling</summary><div class="inside"><p>Each engine has five lifecycle sessions (ready → start → e1m1 → quit), five independent fresh launches directly into start, and five independent fresh menu-to-server connection sessions. Engine order rotates between rounds. Isolated portable copies use identical stock pak0/pak1 bytes, 800 × 600 verified physical client area, FOV 90, VSync/MSAA off, sound on, and requested 144 FPS limits. OS file and driver caches remain warm; this is not a reboot or cold-storage benchmark.</p><p>Engine-native frame waits settle between actions; FTE uses deferred <code>in</code> timers because map changes clear queued commands. ezQuake's startup flush ignores waits, so native <code>serverexec</code> stages and <code>f_spawn</code> callbacks schedule its actions after initialization. Settling is excluded from command-to-ready timings. The ezQuake readiness marker runs in its native runtime buffer after queued renderer configuration; five replacement lifecycle sessions were collected separately after this endpoint audit. Their original attempts are excluded and retained in the raw evidence. Quakespasm-family clients use <code>-listen 8</code> to suppress focus sleep, with UDP enabled and the client explicitly bound to loopback; deathmatch and coop are disabled. Native renderer/network scheduling still differs. Local connection sessions target the isolated servers. Denver sessions target the public server after untimed permission-dialog setup, with no player input during timing. Native auxiliary-file requests are included: ezQuake requests a missing base skin, and QSS-M may request missing location files; the local server's missing-file responses remain part of joining. FTE uses native -noupdates so update prompts do not block its sessions. Other background release/add-on checks may still occur under shipped defaults.</p><p>NetQuake local servers use vanilla protocol 15 (including vkQuake's verified <code>sv_protocol Base-15</code> setting). The hidden QSS-M dedicated connection server runs e1m1 at 127.0.0.1:26001. The separate hidden FTE QuakeWorld server serves the same stock game data at 127.0.0.1:27501. Released engine packages retain bundled assets and built-in map/entity fixes; these can differ between engines. Rendering uses Vulkan in vkQuake and OpenGL in the others.</p><p>The host clock is Python <code>perf_counter</code>. Native logs are polled every 1 ms; OS scheduling and console flushing add measurement uncertainty. Medians and observed min–max ranges describe only these five local runs. Quit ends at actual process exit. Commands, configurations, hashes, every observed event, and exit codes are retained in the raw JSON.</p></div></details>
<details open><summary>Denver connection setup and cleanup</summary><div class="inside"><p>{html.escape(remote["method"])}</p><p>Server: CRMod 7 on QSS-M 1.6.9-1aa5a7d. Target uses explicit port 26000; FTE uses native <code>connectnq</code>. All five compatible clients received identical aerowalk BSP and {len(remote["cached_resources"])} cached custom sound files. The main command buffer stays empty during signon, allowing server handshake commands to execute. Protocol settings and cleanup differ from the local sessions and are retained in each raw row. Connection timing ends before cleanup; public-server quit time is not measured.</p><p>Server status, map, player counts, resource hashes, configurations, native events, and logs are retained in the raw JSON. ezQuake remains benchmarked on the compatible local QuakeWorld server.</p></div></details><details><summary>Build identity and lifecycle configuration</summary><div class="inside">{configs}</div></details>
<details><summary>Evidence and reproduction</summary><div class="inside"><div class="artifact-links"><a href="results.json" download>Raw lifecycle results</a><a href="samples.csv" download>All lifecycle samples (CSV)</a><a href="lifecycle/benchmark.py">Lifecycle measurement script</a><a href="lifecycle/data-verification.json">Independent data verification</a><a href="README.md">Reproduction notes</a></div><p style="margin-top:16px">The JSON exports work offline. The top export includes both complete raw datasets; each section also offers its own JSON. Unavailable connection endpoints stay blank in CSV and null in summarized samples.</p></div></details></section><footer><span>Engine Lab · Local measurements, transparent evidence.</span><span>6 engines · 7 lifecycle tests · 9 FPS maps</span></footer></main></body></html>'''
 (OUT/'index.html').write_text(head+body,encoding='utf-8')
 if not (OUT/'original-results.json').exists():shutil.copy2(OUT/'results.json',OUT/'original-results.json')
 (OUT/'results.json').write_text(json.dumps(raw,indent=2),encoding='utf-8')
 dest=OUT/'lifecycle';dest.mkdir(exist_ok=True)
 for f in ('benchmark.py','build_report.py','lifecycle-section.html','server-nq.log','server-qw.log'):shutil.copy2(ROOT/f,dest/f)
 shutil.copytree(ROOT/'logs',dest/'logs',dirs_exist_ok=True)
 if (ROOT/'excluded-logs').exists():shutil.copytree(ROOT/'excluded-logs',dest/'excluded-logs',dirs_exist_ok=True)
 shutil.copytree(ROOT/'remote/logs',dest/'remote/logs',dirs_exist_ok=True)
 for folder in ('excluded-logs','setup-logs','diagnostic-logs'):
  if (ROOT/'remote'/folder).exists():shutil.copytree(ROOT/'remote'/folder,dest/'remote'/folder,dirs_exist_ok=True)
 for f in ('remote.py','preflight.py','udp_cleanup.py'):shutil.copy2(ROOT/f,dest/f)
 shutil.copy2(ROOT/'remote/SETUP-NOTES.md',dest/'remote/SETUP-NOTES.md')
 with (OUT/'samples.csv').open('w',newline='',encoding='utf-8') as f:
  w=csv.writer(f);w.writerow(['engine','run']+[m['key']+'_ms' for m in metrics])
  for k,e in eng.items():
   for i in range(5):w.writerow([e['name'],i+1]+['' if m['stats'][k]['samples'][i] is None else f"{m['stats'][k]['samples'][i]:.6f}" for m in metrics])
 (OUT/'README.md').write_text('''# Six-engine lifecycle and FPS benchmarks

Open index.html directly: the dashboard and raw JSON exports work offline. Startup, quit, first/subsequent map loads, launch-to-map, and local joining now cover QSS-M, FTEQW, ezQuake, Ironwail, vkQuake, and the requested March 1 2024 QSS archive. Five rounds produce 90 local sessions plus 25 Denver connection sessions, with 205 positive timings. Denver uses aerowalk at explicit port 26000, with identical cached map/sounds and an empty server before every attempt. ezQuake is inapplicable to this NetQuake endpoint. FTE local sessions were repeated with native -noupdates after confirming its startup update-source prompt; prior FTE attempts are excluded. ezQuake uses a separate QuakeWorld server and is not ranked against NetQuake connection times.

results.json and samples.csv contain the final six-engine lifecycle run. lifecycle/logs contains the 90 local native logs; lifecycle/remote/logs contains the 25 Denver logs. Excluded FTE/ezQuake attempts and rotated-map Denver attempts are retained separately. fps-results.json, fps-samples.csv, and fps/logs preserve the existing 324 FPS passes, with 54 warm-ups and 270 measured passes. original-results.json and the old top-level logs preserve the superseded two-engine lifecycle baseline and are excluded from current summaries.

## Reproduce lifecycle measurements

Use Windows with Python 3. Supply licensed pak0.pak and pak1.pak under fps/assets/id1 (game data is intentionally omitted). Set the four released executable paths in fps/acquisition.json and the installed QSS-M/FTE paths in fps/measure.py. The lifecycle harness imports only native package preparation helpers from that module; it does not run FPS tests. Run from a preserved copy of this report directory to avoid replacing its measurements, then run python benchmark.py --pilot followed by python benchmark.py. Run on the normal Windows desktop; the sandbox's separate desktop can cause focus throttling or hidden dialogs. Ports 26001, 26002, and 27501 must be free.

Executables, DLLs, and game data are copied into isolated engine directories. Native configuration scripts are also stored in a small pak2.pak to make search precedence reliable. Only benchmark-owned processes are quit or terminated on failure. Dedicated servers bind loopback, remain private, and receive native authenticated quit packets. vkQuake's native preference-directory qconsole.log is read without editing user settings; unique session markers reject stale output. Source paths, executable/data hashes, commands, staged scripts, events, exit codes, and physical window sizes are retained in results.json. Read the HTML methodology before interpreting the results.

For Denver, approve permission dialogs during untimed preflight.py setup, then run remote.py. The user-authorized cmd dm normal aerowalk command restores the map only on an empty server. Native signon stage 4 ends timing. FTE and QSS-M disconnect through native post-signon callbacks; other owned clients are terminated after readiness, then the runner sends only clc_disconnect using exclusively reclaimed, verified client-owned UDP source ports. No public-session quit timing is measured. Server status/map/player count is checked around each attempt. Do not mix rotated-map attempts. Cached custom resource hashes and source-port ownership are retained in results.json.

build_report.py is the local generation script; it expects the sibling scratch FPS generator/template directory used for this run. For standalone reproduction, inspect raw logs/results directly or adapt its template paths. FPS reproduction is described in fps/measure.py and fps/record.py; supply the licensed assets and the retained shared demos.
''',encoding='utf-8')
 print(json.dumps({'verified':raw['verified'],'medians':{m['key']:{k:round(s['median'],2) if s['median'] is not None else None for k,s in m['stats'].items()} for m in metrics}},indent=2))

if __name__=='__main__':main()
