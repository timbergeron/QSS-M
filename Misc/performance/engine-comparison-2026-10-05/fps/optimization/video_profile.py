import sys,json,shutil,subprocess,time,os,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT));import measure
rows=[]
for name,width,height,full in [('window_800',800,600,False),('fullscreen_800',800,600,True),('window_4k',3840,2160,False)]:
 base=ROOT/'video'/name;base.mkdir(parents=True,exist_ok=True)
 for f in (ROOT/'bin-video').iterdir():
  if f.is_file() and f.suffix.lower() in ('.exe','.dll','.pdb'):shutil.copy2(f,base/f.name)
 shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True);shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
 cfg=measure.COMMON.replace('vid_width 800',f'vid_width {width}').replace('vid_height 600',f'vid_height {height}').replace('vid_fullscreen 0',f'vid_fullscreen {int(full)}')+measure.SPEC['qssm']+'echo BENCH_VIDEO\nplaydemo fps_aerowalk\n'
 rc='exec default.cfg\nexec fpsbench.cfg\n';measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
 (base/'id1/quake.rc').write_text(rc);(base/'id1/fpsbench.cfg').write_text(cfg)
 for f in base.rglob('*.log'):f.unlink()
 args=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-nojoy','-noice','-condebug','-benchvid','-fullscreen' if full else '-window','-width',str(width),'-height',str(height)]
 env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local')
 with (base/'stdout.log').open('wb') as out:
  p=subprocess.Popen(args,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT);start=time.monotonic();txt='';result=None
  try:
   while time.monotonic()-start<30:
    log=base/'qconsole.log'
    if log.exists():
     txt=log.read_text(errors='replace');m=re.search(r'BENCH_VID (.*)',txt)
     if m:
      result={k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',m[1])};break
    if p.poll() is not None:break
    time.sleep(.05)
   geometry=measure.window_pixels(p.pid)
  finally:
   if p.poll() is None:p.terminate();p.wait(timeout=10)
 row={'condition':name,'result':result,'geometry':geometry,'command':args,'config':cfg}
 rows.append(row);(ROOT/'video/results.json').write_text(json.dumps(rows,indent=2));print(row,flush=True)
 if result is None:raise RuntimeError('Missing video timings')
