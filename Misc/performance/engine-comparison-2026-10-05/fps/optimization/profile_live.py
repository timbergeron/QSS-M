"""Sample an isolated optimized source build during ordinary demo playback."""
import sys,json,shutil,subprocess,time,os,hashlib,pickle
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT));import measure
sys.path.insert(0,str(ROOT.parent/'claude-prof'));from sampler import Sampler,symbolize,report
base=ROOT/'live_profile';base.mkdir(exist_ok=True)
for f in (ROOT/'bin').iterdir():
 if f.is_file() and f.suffix.lower() in ('.exe','.dll','.pdb'):shutil.copy2(f,base/f.name)
shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
cfg=measure.COMMON+measure.SPEC['qssm']+'echo PROFILE_LIVE\nplaydemo fps_aerowalk\n'
rc='exec default.cfg\nexec fpsbench.cfg\n'
for f in base.rglob('*.log'):f.unlink()
(base/'id1/fpsbench.cfg').write_text(cfg);(base/'id1/quake.rc').write_text(rc)
measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
args=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-noice','-nojoy','-condebug','-window','-width','800','-height','600']
env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local')
with (base/'stdout.log').open('wb') as out:
 p=subprocess.Popen(args,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT)
 try:
  start=time.monotonic()
  while time.monotonic()-start<30:
   log=base/'qconsole.log'
   if log.exists() and '#Aerowalk' in log.read_text(errors='replace'):break
   if p.poll() is not None:raise RuntimeError('Profile client exited')
   time.sleep(.05)
  else:raise RuntimeError('Map did not load')
  time.sleep(2)
  s=Sampler(p.pid,str(base));tid,util,raw,mods=s.run(8,hz=500)
  stacks=symbolize(raw,mods);(ROOT/'live_profile.txt').write_text(report(stacks))
  pickle.dump(stacks,(ROOT/'live_profile.pkl').open('wb'))
  (ROOT/'live_profile.json').write_text(json.dumps({'exe_sha256':hashlib.sha256((base/'quakespasm.exe').read_bytes()).hexdigest(),'command':args,'config':cfg,'geometry':measure.window_pixels(p.pid),'thread':tid,'utilization':util,'samples':len(stacks)},indent=2))
  print(report(stacks,top=12),flush=True)
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=10)
