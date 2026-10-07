"""Disposable shader-state cache hypothesis test; same binary with skipping off/on. No installed configuration edits."""
import sys,json,shutil,subprocess,time,os,statistics,hashlib,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT));import measure
session=ROOT/'uniform-probe'/time.strftime('%Y%m%d-%H%M%S');session.mkdir(parents=True)
rows=[]
for width,height in [(800,600),(3840,2160)]:
    base=session/f'{width}x{height}';base.mkdir()
    for f in (ROOT/'bin-uniform').iterdir():
        if f.is_file() and f.suffix.lower() in ['.exe','.dll']:shutil.copy2(f,base/f.name)
    shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
    shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
    sequence=[(block,mode,i) for block,mode in enumerate([0,1,1,0]) for i in range(6)]
    cfg=measure.COMMON.replace('vid_width 800',f'vid_width {width}').replace('vid_height 600',f'vid_height {height}')+measure.SPEC['qssm']+'echo UNIFORM_PROBE\n'
    for n,(block,mode,i) in enumerate(sequence):cfg+=f'alias fp_{n} "alias fp_{n} wait;bench_uniform_skip {mode};bench_uniform_stats;echo UNIFORM_PASS_{block}_{mode}_{i};bench_uniform_skip;timedemo fps_aerowalk"\n'
    for n in range(len(sequence)):cfg+=f'fp_{n}\n'+'wait\n'*200
    cfg+='bench_uniform_stats\n'
    rc='exec default.cfg\nexec fpsbench.cfg\n'
    measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
    (base/'id1/quake.rc').write_text(rc);(base/'id1/fpsbench.cfg').write_text(cfg)
    cmd=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-noice','-nojoy','-condebug','-window','-width',str(width),'-height',str(height)]
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
                    values=[{'frames':int(m[1]),'fps':float(m[3])} for m in measure.FPS.finditer(text)]
                    if len(values)>len(data):data=values;print(width,height,sequence[len(data)-1],data[-1],flush=True)
                    if len(data)==len(sequence) and text.count("BENCH_UNIFORM requested=")>=len(sequence)+1:break
                if p.poll() is not None:break
                time.sleep(.04)
        finally:
            if p.poll() is None:p.terminate();p.wait(timeout=10)
    assert len(data)==len(sequence) and geometry==dict(zip(['width','height'],[width,height]))
    assert all(x['frames']==1445 for x in data)
    samples=[dict(x,block=b,bench_uniform_skip=mode,pass_index=i,warmup=i<2) for x,(b,mode,i) in zip(data,sequence)]
    counts=[dict(requested=int(m[1]),sent=int(m[2])) for m in re.finditer(r'BENCH_UNIFORM requested=(\d+) sent=(\d+)',text)]
    assert len(counts)==len(sequence)+1 and counts[0]=={'requested':0,'sent':0}
    for sample,count in zip(samples,counts[1:]):sample.update(count)
    assert all(x['sent']==x['requested'] if x['bench_uniform_skip']==0 else 0<x['sent']<x['requested'] for x in samples)
    summary={str(mode):statistics.median(x['fps'] for x in samples if x['bench_uniform_skip']==mode and not x['warmup']) for mode in [0,1]}
    row={'resolution':[width,height],'samples':samples,'summary':summary,'geometry':geometry,'command':cmd,'config':cfg,'exe_sha256':hashlib.sha256((base/'quakespasm.exe').read_bytes()).hexdigest(),'log':str(base/'qconsole.log')}
    rows.append(row);(session/'results.json').write_text(json.dumps(rows,indent=2));print('SUMMARY',width,height,summary,flush=True)
print('RESULTS',session/'results.json',flush=True)
