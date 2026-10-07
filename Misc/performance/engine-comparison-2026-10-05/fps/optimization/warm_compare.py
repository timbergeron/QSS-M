"""Diagnostic same-process comparison. Original benchmark files remain unchanged."""
import sys,json,shutil,subprocess,os,time,hashlib,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT));import measure
native='--native' in sys.argv
keys=[x for x in sys.argv[1:] if x!='--native'] or ['qssm','ironwail','vkquake','qss','fteqw','ezquake','qssm_msvc']
rows=[]
for key in keys:
 kind='qssm' if key in ['qssm_msvc','qssm_no_obs','qssm_fullscreen'] else key
 base=ROOT/'warm'/key;base.mkdir(parents=True,exist_ok=True)
 source=ROOT/'bin/quakespasm.exe' if key=='qssm_msvc' else FPSROOT/'runs'/kind/measure.SOURCES[kind].name
 for f in source.parent.iterdir():
  if f.is_file() and f.suffix.lower() in ('.exe','.dll','.pdb'):shutil.copy2(f,base/f.name)
 for src,dst in [(FPSROOT/'assets/id1',base/'id1'),(FPSROOT/'demos',base/'id1')]:shutil.copytree(src,dst,dirs_exist_ok=True)
 for f in base.rglob('*.log'):f.unlink()
 count=8
 cfg=measure.COMMON+measure.SPEC[kind]+f'echo WARM_COMPARE_{key}\n'
 if key=='qssm_no_obs':cfg+='scr_obsitems 0\n'
 if key=='qssm_fullscreen':cfg=cfg.replace('vid_fullscreen 0','vid_fullscreen 1')
 for i in range(count):cfg+=f'alias diag_{i} "alias diag_{i} wait;echo DIAG_PASS_{i};timedemo fps_aerowalk"\n'
 if native and kind=='fteqw':
  for i in range(count):cfg+=f'in {1+i*3} diag_{i}\n'
 elif native and kind=='ezquake':
  # Native end-of-demo trigger; no runtime control-file polling.
  for i in range(count):
   (base/'id1'/f'diag-step-{i}.cfg').write_text(f'alias f_demoend "exec diag-step-{i+1}.cfg"\necho DIAG_PASS_{i}\ntimedemo fps_aerowalk\n')
  (base/'id1'/f'diag-step-{count}.cfg').write_text('alias f_demoend ""\necho DIAG_DONE\n')
  cfg+='exec diag-step-0.cfg\n'
 elif native:
  for i in range(count):cfg+=f'diag_{i}\n'+'wait\n'*200
 elif kind=='fteqw':cfg+='alias diag_poll "exec diag-control.cfg;in 0.05 diag_poll"\nin 0.05 diag_poll\n'
 elif kind=='ezquake':cfg+='alias diag_poll "exec diag-control.cfg;wait;diag_poll"\nalias f_spawn "serverexec diag-loop.cfg"\ndiag_0\n'
 else:cfg+='alias diag_poll "exec diag-control.cfg;wait;diag_poll"\ndiag_poll\n'
 for game in ['id1','qw']:
  (base/game).mkdir(exist_ok=True)
  (base/game/'fpsbench.cfg').write_text(cfg)
  (base/game/'config.cfg').write_text(measure.COMMON+measure.SPEC[kind])
  (base/game/'autoexec.cfg').write_text('exec fpsbench.cfg\n' if kind=='ezquake' else '')
  (base/game/'diag-loop.cfg').write_text('diag_poll\n')
  (base/game/'diag-control.cfg').write_text('diag_0\n')
  rc='exec default.cfg\nexec fpsbench.cfg\n' if kind!='fteqw' else 'exec default.cfg\nexec config.cfg\nstuffcmds\n'
  (base/game/'quake.rc').write_text(rc)
  measure.pak(base/game/'pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg,'config.cfg':measure.COMMON+measure.SPEC[kind]})
 args=[str(base/source.name),'-basedir',str(base),'-nohome','-condebug','-window','-width','800','-height','600','-nojoy','-noice']
 if key=='qssm_fullscreen':args[args.index('-window')]='-fullscreen'
 if kind in ['qssm','qss','ironwail','vkquake']:args+=['-listen','-noudp']
 if kind=='qssm':args+=['-noquakeimport']
 if kind=='fteqw':args+=['-noupdates','+exec','fpsbench.cfg']
 if kind=='ezquake':args+=['+cl_verify_qwprotocol','0','+set','vid_win_width','800','+set','vid_win_height','600','+set','vid_width','800','+set','vid_height','600']
 env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local');(base/'profile/local').mkdir(parents=True,exist_ok=True)
 data=[];texts={};geometry=None;deadline=time.monotonic()+50;stage=0;changed=time.monotonic()
 with (base/'stdout.log').open('wb') as out:
  p=subprocess.Popen(args,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT)
  try:
   while time.monotonic()<deadline:
    geometry=measure.window_pixels(p.pid) or geometry
    for log in base.rglob('*.log'):
     try:txt=log.read_text(errors='replace')
     except OSError:continue
     if f'WARM_COMPARE_{key}' not in txt:continue
     texts[str(log.relative_to(base))]=txt
     results=[{'frames':int(m[1]),'seconds':float(m[2]),'fps':float(m[3])} for m in measure.FPS.finditer(txt) if int(m[1])>1000]
     if len(results)>len(data):data=results;changed=time.monotonic();print(key,len(data),data[-1]['fps'],flush=True)
    if len(data)>=count:break
    if not native and len(data)>stage and time.monotonic()-changed>.3:
     stage=len(data)
     for game in ['id1','qw']:(base/game/'diag-control.cfg').write_text(f'diag_{stage}\n')
    if p.poll() is not None:break
    time.sleep(.03)
  finally:
   if p.poll() is None:p.terminate();p.wait(timeout=10)
 logdir=ROOT/'warm/logs';logdir.mkdir(exist_ok=True)
 tag=key+('-native' if native else '')
 (logdir/f'{tag}.log').write_text('\n'.join(f'--- {k} ---\n{v}' for k,v in texts.items()))
 row={'engine':key,'exe_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'results':data,'geometry':geometry,'command':args,'config':cfg,'warmups':2,'median':statistics.median(x['fps'] for x in data[2:]) if len(data)==count else None}
 row['control']='native scheduling without runtime file polling' if native else 'runtime file polling (exploratory, not used for final comparison)'
 rows.append(row);(ROOT/'warm'/('native-'+('-'.join(keys))+'.json' if native else 'results.json')).write_text(json.dumps(rows,indent=2));print('SUMMARY',key,row['median'],flush=True)
 if len(data)!=count:print('INCOMPLETE',key,len(data),flush=True)
