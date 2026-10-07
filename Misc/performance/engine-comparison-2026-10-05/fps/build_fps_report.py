import json, math, statistics, csv, shutil, re, struct, hashlib, os, winreg
from pathlib import Path
ROOT=Path(__file__).resolve().parent
OUT=ROOT.parents[1]/'Misc/performance/engine-comparison-2026-10-05'
def main():
 raw=json.loads((ROOT/'fps-results.json').read_text());acq=raw['acquisition'];eng=raw['engines'];maps=list(acq['maps'])
 lifecycle=json.loads((OUT/'results.json').read_text())
 with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,'HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0') as key:cpu=winreg.QueryValueEx(key,'ProcessorNameString')[0].strip()
 raw['hardware']={'cpu':cpu,'logical_cpus':os.cpu_count(),'gpu':'NVIDIA GeForce RTX 4070','nvidia_driver':'591.86','platform':lifecycle['platform'],'measurement_date':'2026-10-05','physical_client_pixels':[800,600]}
 assert len(raw['samples'])==324
 assert all(x['result'] for x in raw['samples'])
 assert len({(s['engine'],s['map'],s['run'],s['warmup']) for s in raw['samples']})==324
 for s in raw['samples']:
  assert s['result']['line'] in (ROOT/s['log']).read_text(errors='replace'),s['log']
  if 'window_pixels' in s:assert s['window_pixels']=={'width':800,'height':600}
 stats={m:{} for m in maps}
 for key,e in eng.items():
  e['renderer']='Vulkan' if key=='vkquake' else 'OpenGL'
  if key=='qssm':e['version']='1.6.9-7da539b · installed'
  if key=='fteqw':e['version']='git-30-f2576ed · installed'
  if key in acq:e['release_url']=acq[key]['release_url']
  for m in maps:
   allrows=[x for x in raw['samples'] if x['engine']==key and x['map']==m]
   assert len(allrows)==6 and sum(x['warmup'] for x in allrows)==1
   samples=[{'run':x['run'],**x['result']} for x in allrows if not x['warmup']]
   vals=[s['fps'] for s in samples];stats[m][key]={'median':statistics.median(vals),'min':min(vals),'max':max(vals),'samples':samples}
   assert max(s['frames'] for s in samples)-min(s['frames'] for s in samples)<=1
 for m in maps:
  frames=[s['frames'] for k in eng for s in stats[m][k]['samples']]
  assert max(frames)-min(frames)<=1,(m,min(frames),max(frames))
 data={'engines':eng,'stats':stats,'geomean':{k:math.exp(statistics.mean(math.log(stats[m][k]['median']) for m in maps)) for k in eng},'raw':raw}
 baseline=ROOT/'lifecycle.html'
 if not baseline.exists():shutil.copy2(OUT/'index.html',baseline)
 text=baseline.read_text(encoding='utf-8')
 section=(ROOT/'fps-section.html').read_text(encoding='utf-8').replace('__FPS_DATA__',json.dumps(data,separators=(',',':')).replace('</','<\\/'))
 text=text.replace('<section class="method" id="method">',section+'\n<section class="method" id="method">')
 text=text.replace('FTEQW vs QSS-M · Lifecycle benchmark','Quake Engine Lab · Lifecycle & FPS benchmarks')
 text=text.replace('/ lifecycle</span>','/ benchmarks</span>')
 text=text.replace('<a class="method-link" href="#method">','<a class="method-link" href="#fps">Map FPS</a><a class="method-link" href="#method">')
 text=text.replace('<h1>FTEQW <span>vs</span> QSS-M<span>.</span></h1>','<h1>Quake engines<span>,<br>measured.</span></h1>')
 text=text.replace('From launch to first map, from a map change to joining a server. A small, repeatable look at the waiting between playing.','Lifecycle timings for FTEQW and QSS-M, plus nine-map FPS comparisons across six engines. Recorded locally, with every sample available to inspect.')
 text=text.replace('<div class="chips"><span class="chip">','<div class="chips"><span class="chip"><strong>6 engines · 9 FPS maps</strong></span><span class="chip">',1)
 text=text.replace('Quicker in 5 of 5 fully measured comparisons.','Lifecycle: quicker in 5 of 5 fully measured comparisons.')
 text=text.replace('<strong>6 tests</strong> · lower is faster','<strong>6 lifecycle tests</strong> · lower is faster')
 text=text.replace('800 × 600 · OpenGL · sound on','Lifecycle · 800 × 600 · OpenGL · sound on')
 text=text.replace('Vanilla NetQuake · loopback server','Lifecycle · Vanilla NetQuake · loopback server')
 text=text.replace('Lower is faster','Lower is faster')
 text=text.replace('Local lifecycle study · 55 timings · 30 client launches','Local study · 55 lifecycle timings · 270 measured FPS passes')
 (OUT/'index.html').write_text(text,encoding='utf-8')
 (OUT/'fps-results.json').write_text(json.dumps(raw,indent=2))
 with (OUT/'fps-samples.csv').open('w',newline='') as f:
  w=csv.writer(f);w.writerow(['engine','map','run','warmup','frames','seconds','fps','mean_frame_ms'])
  for s in raw['samples']:w.writerow([s['engine'],s['map'],s['run'],s['warmup'],s['result']['frames'],s['result']['seconds'],s['result']['fps'],1000/s['result']['fps']])
 sub=OUT/'fps';sub.mkdir(exist_ok=True)
 for name in ('measure.py','record.py','acquire.py','acquisition.json','demos.json','demo-verification.json','fps-section.html','build_fps_report.py'):shutil.copy2(ROOT/name,sub/name)
 shutil.copytree(ROOT/'demos',sub/'demos',dirs_exist_ok=True)
 (sub/'logs').mkdir(exist_ok=True)
 for s in raw['samples']:shutil.copy2(ROOT/s['log'],sub/'logs'/Path(s['log']).name)
 notes='''

## FPS additions (six engines, nine maps)

The FPS section uses five measured native timedemo passes and one discarded warm-up for every engine/map combination: 324 passes in total, 270 measured. The extra release engines are included in FPS; the original lifecycle section remains FTEQW/QSS-M only. Each pass uses a fresh process. Full commands/configs and native output are in fps-results.json, fps-samples.csv, and fps/logs/. The exact shared demos are in fps/demos/.

Benchmarks ran with idle/background throttling disabled: sys_throttle -1 in QSS-M/QSS, sys_inactivesleep 0 in ezQuake, and -listen -noudp in the Quakespasm-family engines. The latter keeps their native background sleep out of timedemo without enabling UDP networking. No server or active match is run. vkQuake stores its native console log in the Windows preferences directory even in portable mode; the runner reads only the section after a unique run marker and copies it into this report. Its configs and game data remain in the isolated engine directory.

Official latest stable GitHub releases were queried on October 5, 2026: ezQuake 3.6.9, Ironwail v0.8.2, vkQuake 1.36.0. QSS is the specifically requested March 1, 2024 build (revision c885ec2a8cabb9dba35a9e58bb1ecc88b2e9d266). Archive/executable/BSP/demo SHA-256 hashes are retained. FTEQW and QSS-M are the same installed binaries as the lifecycle tests.

For reproduction, adapt paths in fps/acquire.py, fps/record.py, and fps/measure.py to your game data and binaries, then run those scripts in that order using Python 3 on Windows. Acquisition creates clean assets and downloads the requested engines; record creates protocol-15 demos; measure collects timedemo output. The supplied scripts originally ran from an ignored .codex-build directory; ROOT-relative paths and REPO/OUT calculations may need adjustment if you run the saved copies elsewhere. This report is static; rerunning measurements does not automatically rebuild it. Original copyrighted game data and downloaded engine packages are not embedded in the report.

These are short, stationary camera sweeps over one spawn's visible geometry. CTF-specific logic, live combat, internet/server behavior, stutter, 1% lows, and image-quality parity are outside this FPS test. Average-frame-time view is 1000/FPS. Read the full HTML methodology before drawing engine-wide conclusions.
'''
 readme=OUT/'README.md';old=readme.read_text(encoding='utf-8');readme.write_text(old.split('\n## FPS additions')[0]+notes,encoding='utf-8')
 verification={'passes':324,'measured_passes':270,'warmups':54,'maps':maps,'engines':list(eng),'frames_match_within':1,'geometric_mean_fps':data['geomean']}
 (OUT/'fps-data-verification.json').write_text(json.dumps(verification,indent=2))
 print(json.dumps(verification,indent=2))
if __name__=='__main__':main()
