"""Aggregate timer diagnostic; not a ranking/performance comparison."""
import sys,json,shutil,subprocess,time,os,re,hashlib,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT)); import measure
base=ROOT/'timedemo-cpu'/time.strftime('%Y%m%d-%H%M%S');base.mkdir(parents=True)
for f in (ROOT/'bin-td').iterdir():
    if f.is_file() and f.suffix.lower() in ('.exe','.dll','.pdb'):shutil.copy2(f,base/f.name)
shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
maps=['aerowalk','dm3','ctf2m8']
sequence=[(m,i) for m in maps for i in range(8)]
cfg=measure.COMMON+measure.SPEC['qssm']+'echo PROFILE_TIMEDEMO_CPU\n'
for n,(m,i) in enumerate(sequence):cfg+=f'alias td_{n} "alias td_{n} wait;echo TD_PASS_{m}_{i};timedemo fps_{m}"\n'
for n in range(len(sequence)):cfg+=f'td_{n}\n'+'wait\n'*200
rc='exec default.cfg\nexec fpsbench.cfg\n'
measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
(base/'id1/quake.rc').write_text(rc);(base/'id1/fpsbench.cfg').write_text(cfg)
cmd=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-noice','-nojoy','-condebug','-benchtd','-window','-width','800','-height','600']
env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local')
start=time.monotonic();data=[];geometry=None
with (base/'stdout.log').open('wb') as out:
    p=subprocess.Popen(cmd,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT)
    try:
        while time.monotonic()-start<150:
            geometry=measure.window_pixels(p.pid) or geometry
            log=base/'qconsole.log'
            if log.exists():
                text=log.read_text(errors='replace')
                fps=[float(m[3]) for m in measure.FPS.finditer(text)]
                values=[{k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',m[1])} for m in re.finditer(r'BENCH_TD (.*)',text)]
                if len(values)>len(data):
                    data=values;print(sequence[len(data)-1],data[-1],flush=True)
                if len(data)==len(sequence):break
            if p.poll() is not None:break
            time.sleep(.04)
    finally:
        if p.poll() is None:p.terminate();p.wait(timeout=10)
assert len(data)==len(sequence) and geometry=={'width':800,'height':600}
rows=[dict(x,map=m,pass_index=i,warmup=i<2,fps=fps[n]) for n,(x,(m,i)) in enumerate(zip(data,sequence))]
summary={m:{k:statistics.median(x[k] for x in rows if x['map']==m and not x['warmup']) for k in ['read_us','next_us','finish_us','fps']} for m in maps}
(base/'results.json').write_text(json.dumps({'samples':rows,'summary':summary,'geometry':geometry,'command':cmd,'config':cfg,'exe_sha256':hashlib.sha256((base/'quakespasm.exe').read_bytes()).hexdigest(),'log':str(base/'qconsole.log'),'method':'Disposable timers around CL_ReadFromServer and rewind snapshot/event phases. Two warm-ups then six diagnostic timedemos per map. Read includes Next/Finish; do not add these overlapping values. These instrumented scores are not a new FPS comparison.'},indent=2))
print('SUMMARY',json.dumps(summary),flush=True)
print('RESULTS',base/'results.json',flush=True)
