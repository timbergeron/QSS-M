"""Isolated QSS-M FPS diagnostics. Does not modify published samples or user configs."""
import json, os, shutil, subprocess, time, re, hashlib
from pathlib import Path
import measure
ROOT=Path(__file__).resolve().parent
BASE=ROOT/'diagnostic/qssm'
BASE.mkdir(parents=True,exist_ok=True)
shutil.copytree(ROOT/'runs/qssm',BASE,dirs_exist_ok=True)
EXE=BASE/'QSS-M-w64.exe'
queries=['r_shadows','r_outline','r_scenecache','r_bmodelcache','r_aliaslightcache','r_softemu','r_dynamic','r_waterwarp','r_fastsky','gl_finish','host_maxfps','vid_vsync','vid_fsaa','r_scale']
variants={'baseline':'','effects_off':'r_shadows 0\nr_outline 0\n','scene_off':'r_scenecache 0\n','scene_on':'r_scenecache 1\n','4k':'vid_width 3840\nvid_height 2160\nvid_restart\n'}
rows=[]
for name,extra in variants.items():
 for index in range(3):
  for p in BASE.rglob('*.log'):p.unlink()
  marker=f'DIAG_{name}_{index}'
  cfg=measure.COMMON+measure.SPEC['qssm']+extra+f'echo {marker}\n'+'\n'.join(queries)+'\ntimedemo fps_aerowalk\n'
  rc='exec default.cfg\nexec fpsbench.cfg\n'
  for game in ['id1','qw']:
   (BASE/game/'fpsbench.cfg').write_text(cfg)
   (BASE/game/'quake.rc').write_text(rc)
   (BASE/game/'config.cfg').write_text(measure.COMMON+measure.SPEC['qssm'])
   measure.pak(BASE/game/'pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg,'config.cfg':measure.COMMON+measure.SPEC['qssm']})
  args=[str(EXE),'-basedir',str(BASE),'-nohome','-condebug','-window','-width','800','-height','600','-nojoy','-noice','-listen','-noudp','-noquakeimport']
  env=os.environ.copy();env['APPDATA']=str(BASE/'profile');env['LOCALAPPDATA']=str(BASE/'profile/local')
  with (BASE/'stdout.log').open('wb') as out:
   p=subprocess.Popen(args,cwd=BASE,env=env,stdout=out,stderr=subprocess.STDOUT)
   start=time.monotonic();txt='';result=None;geometry=None
   try:
    while time.monotonic()-start<30:
     geometry=measure.window_pixels(p.pid) or geometry
     log=BASE/'qconsole.log'
     if log.exists():
      txt=log.read_text(errors='replace')
      matches=list(measure.FPS.finditer(txt[txt.find(marker):])) if marker in txt else []
      if matches:
       m=matches[-1];result={'frames':int(m[1]),'seconds':float(m[2]),'fps':float(m[3])};break
     if p.poll() is not None:break
     time.sleep(.05)
   finally:
    if p.poll() is None:p.terminate();p.wait(timeout=10)
  logpath=ROOT/f'diagnostic/{name}-{index}.log';logpath.write_text(txt)
  row={'variant':name,'index':index,'result':result,'geometry':geometry,'config':cfg,'log':str(logpath.relative_to(ROOT))}
  rows.append(row);(ROOT/'diagnostic/results.json').write_text(json.dumps({'exe_sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'samples':rows},indent=2))
  print(name,index,result,geometry,flush=True)
  if result is None:raise RuntimeError('Failed diagnostic')
