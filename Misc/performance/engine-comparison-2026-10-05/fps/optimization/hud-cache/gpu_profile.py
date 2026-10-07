"""Fixed 2–12 second playback interval; sampled GPU queries, owned processes."""
import sys, json, shutil, subprocess, time, os, re, hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT)); import measure
session=ROOT/'gpu'/time.strftime('%Y%m%d-%H%M%S')
rows=[]
for index,(name,width,height,full) in enumerate([
    ('window_800',800,600,False), ('fullscreen_800',800,600,True),
    ('fullscreen_800',800,600,True), ('window_800',800,600,False),
    ('window_4k',3840,2160,False), ('window_4k',3840,2160,False)]):
    base=session/f'{index:02}-{name}'; base.mkdir(parents=True)
    for f in (ROOT/'bin-gpu').iterdir():
        if f.is_file() and f.suffix.lower() in ('.exe','.dll','.pdb'): shutil.copy2(f,base/f.name)
    shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
    shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
    cfg=measure.COMMON.replace('vid_width 800',f'vid_width {width}').replace('vid_height 600',f'vid_height {height}').replace('vid_fullscreen 0',f'vid_fullscreen {int(full)}')+measure.SPEC['qssm']+'echo BENCH_GPU_PLAYBACK\nplaydemo fps_aerowalk\n'
    rc='exec default.cfg\nexec fpsbench.cfg\n'
    measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
    (base/'id1/quake.rc').write_text(rc); (base/'id1/fpsbench.cfg').write_text(cfg)
    args=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-nojoy','-noice','-condebug','-benchgpu','-fullscreen' if full else '-window','-width',str(width),'-height',str(height)]
    env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local')
    with (base/'stdout.log').open('wb') as out:
        p=subprocess.Popen(args,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT)
        start=time.monotonic(); result=None; geometry=None
        try:
            while time.monotonic()-start<30:
                geometry=measure.window_pixels(p.pid) or geometry
                log=base/'qconsole.log'
                if log.exists():
                    txt=log.read_text(errors='replace');m=re.search(r'BENCH_GPU frames=(.*)',txt)
                    if m:
                        result={k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)','frames='+m[1])};break
                if p.poll() is not None: break
                time.sleep(.05)
        finally:
            if p.poll() is None:p.terminate();p.wait(timeout=10)
    if geometry!={'width':width,'height':height}:raise RuntimeError(f'Wrong drawable size: {geometry}')
    row={'condition':name,'result':result,'geometry':geometry,'command':args,'config':cfg,'exe_sha256':hashlib.sha256((base/'quakespasm.exe').read_bytes()).hexdigest(),'log':str(base/'qconsole.log')}
    rows.append(row);(session/'results.json').write_text(json.dumps(rows,indent=2));print(name,result,flush=True)
    if result is None:raise RuntimeError('Missing GPU timing results; inspect retained log')
print('RESULTS',session/'results.json',flush=True)
