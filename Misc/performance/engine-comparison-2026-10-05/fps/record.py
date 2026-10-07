import shutil, struct, re, time, json, hashlib, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
BASE=ROOT/'recorder'
BASE.mkdir(exist_ok=True)
for f in Path('C:/Users/Tim Bergeron/Desktop/qssm').iterdir():
 if f.suffix.lower() in ('.exe','.dll','.pak') and f.is_file(): shutil.copy2(f,BASE/f.name)
shutil.copytree(ROOT/'assets/id1',BASE/'id1',dirs_exist_ok=True)
(BASE/'id1/quake.rc').write_text('exec default.cfg\nexec config.cfg\nstuffcmds\n')
(BASE/'id1/config.cfg').write_text('vid_vsync 0\nhost_maxfps 72\nhost_framerate 0.013888889\ncl_demoreel 0\ncl_autodemo 0\ncl_demo_format dem\nsv_protocol Base-15\ndeathmatch 1\nnomonsters 1\ncl_discord_presence 0\nfov 90\nviewsize 100\n')
meta=json.loads((ROOT/'demos.json').read_text()) if (ROOT/'demos.json').exists() else {}
for name in json.loads((ROOT/'acquisition.json').read_text())['maps']:
  if name in meta:continue
  (BASE/'id1/config.cfg').write_text('vid_vsync 0\nhost_maxfps 72\nhost_framerate 0.013888889\ncl_demoreel 0\ncl_autodemo 0\ncl_demo_format dem\nsv_protocol Base-15\ndeathmatch 1\nnomonsters 1\ncl_discord_presence 0\nfov 90\nviewsize 100\n')
  data=(ROOT/'assets/id1/maps'/f'{name}.bsp').read_bytes()
  offset,size=struct.unpack_from('<ii',data,4)
  entities=[dict(re.findall(r'"([^"]+)"\s+"([^"]*)"',x)) for x in re.findall(r'\{([^}]+)\}',data[offset:offset+size].decode(errors='replace'))]
  spawn=next((x for x in entities if x.get('classname')=='info_player_deathmatch'),None) or next(x for x in entities if x.get('classname')=='info_player_start')
  origin=[float(x) for x in spawn['origin'].split()];origin[2]+=1
  cfg='map '+name+'\n'+'wait\n'*300+'noclip\ngod\nnotarget\nsetpos '+' '.join(map(str,origin))+' 0 0 0\ncl_yawspeed 18\n'+'wait\n'*72+'record fps_'+name+'\n+right\n'+'wait\n'*1440+'-right\nstop\nquit\n'
  (BASE/'id1/record.cfg').write_text(cfg)
  print('Recording '+name,flush=True)
  proc=subprocess.Popen([str(BASE/'QSS-M-w64.exe'),'-basedir',str(BASE),'-nohome','-noquakeimport','-listen','-noudp','-noice','-condebug','-window','-width','800','-height','600','+exec','record.cfg'],cwd=BASE,stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT)
  try:proc.wait(timeout=90)
  finally:
   if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
  if proc.returncode!=0:raise RuntimeError('Recorder failed '+str(proc.returncode))
  demos=list(BASE.rglob('fps_'+name+'.dem'))
  if len(demos)!=1: raise RuntimeError(str(demos))
  target=ROOT/'demos'/demos[0].name;target.parent.mkdir(exist_ok=True);shutil.copy2(demos[0],target)
  meta[name]={'origin':origin,'yaw_rate':18,'duration_seconds':20,'bytes':target.stat().st_size,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
  (ROOT/'demos.json').write_text(json.dumps(meta,indent=2))
  print('Saved '+str(target),flush=True)

