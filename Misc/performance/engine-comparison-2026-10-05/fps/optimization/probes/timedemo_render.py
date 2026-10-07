"""Timedemo stage/GPU profiler: every result is diagnostic, excluded from rankings."""
import sys,json,shutil,subprocess,time,os,re,hashlib,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent;FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT));import measure
session=ROOT/'timedemo-render'/time.strftime('%Y%m%d-%H%M%S');session.mkdir(parents=True)
result=[]
for width,height in [(800,600),(3840,2160)]:
 base=session/f'{width}x{height}';base.mkdir()
 for f in (ROOT/'bin-td-render').iterdir():
  if f.is_file() and f.suffix.lower() in ['.exe','.dll']:shutil.copy2(f,base/f.name)
 shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
 shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
 maps=['aerowalk','dm3','ctf2m8'];sequence=[(m,i) for m in maps for i in range(8)]
 cfg=measure.COMMON.replace('vid_width 800',f'vid_width {width}').replace('vid_height 600',f'vid_height {height}')+measure.SPEC['qssm']+'echo PROFILE_TD_RENDER\n'
 for n,(m,i) in enumerate(sequence):
  cfg+=f'alias tr_{n} "alias tr_{n} wait;bench_td_render_reset;echo TD_RENDER_PASS_{m}_{i};timedemo fps_{m}"\n'
 for n in range(len(sequence)):cfg+=f'tr_{n}\n'+'wait\n'*200+'bench_td_render_stats\n'
 rc='exec default.cfg\nexec fpsbench.cfg\n';measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
 (base/'id1/quake.rc').write_text(rc);(base/'id1/fpsbench.cfg').write_text(cfg)
 cmd=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-noice','-nojoy','-condebug','-benchtdrender','-window','-width',str(width),'-height',str(height)]
 env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local')
 start=time.monotonic();data=[];geometry=None
 with (base/'stdout.log').open('wb') as out:
  p=subprocess.Popen(cmd,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT)
  try:
   while time.monotonic()-start<180:
    geometry=measure.window_pixels(p.pid) or geometry
    log=base/'qconsole.log'
    if log.exists():
     text=log.read_text(errors='replace')
     fps=[{'native_frames':int(m[1]),'fps':float(m[3])} for m in measure.FPS.finditer(text)]
     values=[{k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',m[1])} for m in re.finditer(r'BENCH_TD_RENDER (.*)',text)]
     if len(values)>len(data):data=values;print(width,height,sequence[len(data)-1],data[-1],flush=True)
     if len(data)==len(sequence):break
    if p.poll() is not None:break
    time.sleep(.04)
  finally:
   if p.poll() is None:p.terminate();p.wait(timeout=10)
 assert len(data)==len(sequence)==len(fps) and geometry==dict(zip(['width','height'],[width,height]))
 rows=[dict(x,**fps[n],map=m,pass_index=i,warmup=i<2) for n,(x,(m,i)) in enumerate(zip(data,sequence))]
 assert all(s['native_frames']==(1445 if s['map']=='aerowalk' else 1442) for s in rows)
 assert all(s['samples']>=80 and abs(s['frames']-s['native_frames'])<=2 for s in rows)
 summary={m:{k:statistics.median(s[k] for s in rows if s['map']==m and not s['warmup']) for k in ['world_us','hud_us','swap_us','other_us','gpu_us','fps']} for m in maps}
 block={'resolution':[width,height],'samples':rows,'summary':summary,'geometry':geometry,'command':cmd,'config':cfg,'exe_sha256':hashlib.sha256((base/'quakespasm.exe').read_bytes()).hexdigest(),'log':str(base/'qconsole.log')}
 result.append(block)
 (session/'results.json').write_text(json.dumps({'method':'Disposable release build, observer icon cache enabled; native timedemo, two discarded warmups then six measured profiler passes for each of three maps at each resolution. CPU 3D interval begins before GL_BeginRendering and ends after V_RenderView; HUD interval covers remaining drawing and post-processing before swap; outside interval begins after prior swap and excludes first-frame predecessor. GPU timestamp pairs bracket render/HUD, sampled 1 per 16 frames, results polled only when available, drained during 200 native wait frames before logging. GPU and CPU intervals overlap and are not additive. Instrumented FPS excluded from ranking and source A/B results.','blocks':result},indent=2))
 print('SUMMARY',width,height,json.dumps(summary),flush=True)
print('RESULTS',session/'results.json',flush=True)
