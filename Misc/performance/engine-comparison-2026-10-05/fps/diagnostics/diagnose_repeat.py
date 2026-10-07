import json,os,subprocess,time,hashlib,sys
from pathlib import Path
import measure
ROOT=Path(__file__).resolve().parent
BASE=ROOT/'diagnostic/qssm'
rows=[]
variants=[('baseline_repeat',800,600,''),('fastsky_repeat',800,600,'r_fastsky 2\nr_waterwarp 0\nr_drawflame 0\n'),('scene_off_repeat',800,600,'r_scenecache 0\n'),('4k_repeat',3840,2160,'')]
variants += [('4k_fastsky_repeat',3840,2160,'r_fastsky 2\nr_waterwarp 0\nr_drawflame 0\n'),('menu_settle_repeat',800,600,'wait\n'*200)]
if len(sys.argv)>1:
 variants=[v for v in variants if v[0] in sys.argv[1:]]
 rows=json.loads((ROOT/'diagnostic/repeat-results.json').read_text())
for name,width,height,extra in variants:
 for p in BASE.rglob('*.log'):p.unlink()
 cfg=measure.COMMON.replace('vid_width 800',f'vid_width {width}').replace('vid_height 600',f'vid_height {height}')+measure.SPEC['qssm']+extra+'echo DIAG_REPEAT\n'
 for i in range(8):cfg+=f'echo PASS_{i}\ntimedemo fps_aerowalk\n'+'wait\n'*200
 rc='exec default.cfg\nexec fpsbench.cfg\n'
 for game in ['id1','qw']:
  (BASE/game/'fpsbench.cfg').write_text(cfg);(BASE/game/'quake.rc').write_text(rc)
  measure.pak(BASE/game/'pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
 args=[str(BASE/'QSS-M-w64.exe'),'-basedir',str(BASE),'-nohome','-condebug','-window','-width',str(width),'-height',str(height),'-nojoy','-noice','-listen','-noudp','-noquakeimport']
 env=os.environ.copy();env['APPDATA']=str(BASE/'profile');env['LOCALAPPDATA']=str(BASE/'profile/local')
 with (BASE/'stdout.log').open('wb') as out:
  p=subprocess.Popen(args,cwd=BASE,env=env,stdout=out,stderr=subprocess.STDOUT);start=time.monotonic();geometry=None;txt='';matches=[]
  try:
   while time.monotonic()-start<45:
    geometry=measure.window_pixels(p.pid) or geometry
    log=BASE/'qconsole.log'
    if log.exists():
     txt=log.read_text(errors='replace');matches=list(measure.FPS.finditer(txt))
     if len(matches)>=8:break
    if p.poll() is not None:break
    time.sleep(.05)
  finally:
   if p.poll() is None:p.terminate();p.wait(timeout=10)
 (ROOT/f'diagnostic/{name}.log').write_text(txt)
 row={'variant':name,'geometry':geometry,'fps':[float(m[3]) for m in matches],'frames':[int(m[1]) for m in matches],'extra':extra,'command':args}
 rows=[r for r in rows if r['variant']!=name];rows.append(row);print(row,flush=True)
 (ROOT/'diagnostic/repeat-results.json').write_text(json.dumps(rows,indent=2))
 if len(matches)!=8:raise RuntimeError('Missing repeats')
