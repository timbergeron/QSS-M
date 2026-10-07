"""Native timedemo runner; one owned engine process at a time, isolated data/configs."""
import json, shutil, subprocess, time, re, hashlib, argparse, statistics, struct, os
from pathlib import Path
ROOT=Path(__file__).resolve().parent
META=json.loads((ROOT/'acquisition.json').read_text())
NAMES={'qssm':'QSS-M','fteqw':'FTEQW','ezquake':'ezQuake','ironwail':'Ironwail','vkquake':'vkQuake','qss':'QSS'}
SOURCES={k:Path(META[k]['executables'][0]) for k in META if k!='maps'}
SOURCES.update(qssm=Path('C:/Users/Tim Bergeron/Desktop/qssm/QSS-M-w64.exe'),fteqw=Path('C:/Users/Tim Bergeron/Desktop/Engines/FTE Stuff/FTE/fteqw64.exe'))
COMMON='vid_width 800\nvid_height 600\nvid_fullscreen 0\nvid_vsync 0\nfov 90\nviewsize 100\n'
SPEC={
 'qssm':'host_maxfps 0\nsys_throttle -1\ncl_demoreel 0\ncl_discord_presence 0\nr_scale 1\nvid_fsaa 0\n',
 'qss':'host_maxfps 0\nsys_throttle -1\nr_scale 1\nvid_fsaa 0\n',
 'ironwail':'host_maxfps 0\nr_scale 1\nvid_fsaa 0\n',
 'vkquake':'host_maxfps 0\nr_scale 1\nvid_fsaa 0\n',
 'fteqw':'vid_renderer gl\ncl_maxfps 0\ncl_idlefps 0\nvid_multisample 0\nlog_readable 1\n',
 'ezquake':'cl_maxfps 0\ncl_physfps 77\nsys_inactivesleep 0\nvid_vsync 0\nvid_win_width 800\nvid_win_height 600\nvid_framebuffer_scale 1\nlog fpsbench\n'}
def pak(path,files):
 payload=bytearray(b'PACK'+b'\0'*8);directory=bytearray()
 for name,text in files.items():
  data=text.encode();directory+=struct.pack('<56sii',name.encode(),len(payload),len(data));payload+=data
 struct.pack_into('<ii',payload,4,len(payload),len(directory));path.write_bytes(payload+directory)
FPS=re.compile(r'(\d+) frames\s+([\d.]+) seconds\s+([\d.]+) fps')
def window_pixels(pid):
 """Read-only physical client-area diagnostics for this owned process."""
 import ctypes
 from ctypes import wintypes as w
 u=ctypes.windll.user32
 u.SetThreadDpiAwarenessContext.argtypes=[ctypes.c_void_p];u.SetThreadDpiAwarenessContext.restype=ctypes.c_void_p
 prior=u.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4));found=[]
 cbtype=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
 def visit(hwnd,param):
  owner=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
  if owner.value==pid and u.IsWindowVisible(hwnd):
   rect=w.RECT();u.GetClientRect(hwnd,ctypes.byref(rect));width=rect.right-rect.left;height=rect.bottom-rect.top
   if width>200 and height>200:found.append({'width':width,'height':height})
  return True
 u.EnumWindows(cbtype(visit),0)
 if prior:u.SetThreadDpiAwarenessContext(prior)
 return max(found,key=lambda x:x['width']*x['height']) if found else None
def prepare(key):
 base=ROOT/'runs'/key;base.mkdir(parents=True,exist_ok=True)
 source=SOURCES[key]
 if key in ('qssm','fteqw'):
  shutil.copy2(source,base/source.name)
  for dll in source.parent.glob('*.dll'):shutil.copy2(dll,base/dll.name)
 else:shutil.copytree(source.parent,base,dirs_exist_ok=True)
 shutil.copytree(ROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
 shutil.copytree(ROOT/'demos',base/'id1',dirs_exist_ok=True)
 for game in ('id1','qw'):
  (base/game).mkdir(exist_ok=True)
 return base,base/source.name
def run(key,mapname,index,warmup=False,pilot=False):
 base=ROOT/'runs'/key;exe=base/SOURCES[key].name
 for f in base.rglob('*.log'): f.unlink(missing_ok=True)
 for f in base.rglob('config.cfg'):f.unlink(missing_ok=True)
 marker=f'FPS_BENCH_{key}_{mapname}_{time.time_ns()}'
 cfg=COMMON+SPEC[key]+f'echo {marker}\nvid_width\nvid_height\nvid_vsync\ntimedemo fps_{mapname}\n'
 rc='exec default.cfg\nexec fpsbench.cfg\n' if key!='fteqw' else 'exec default.cfg\nexec config.cfg\nstuffcmds\n'
 for game in ('id1','qw'):
  (base/game/'quake.rc').write_text(rc)
  (base/game/'config.cfg').write_text(COMMON+SPEC[key])
  (base/game/'fpsbench.cfg').write_text(cfg)
  (base/game/'autoexec.cfg').write_text('exec fpsbench.cfg\n' if key=='ezquake' else '')
  pak(base/game/'pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg,'config.cfg':COMMON+SPEC[key]})
 args=[str(exe),'-basedir',str(base),'-nohome','-condebug','-window','-width','800','-height','600','-nojoy','-noice']
 if key in ('qssm','qss','ironwail','vkquake'):args+=['-listen','-noudp']
 if key=='qssm':args+=['-noquakeimport']
 if key=='fteqw':args+=['+exec','fpsbench.cfg']
 if key=='ezquake':args+=['+cl_verify_qwprotocol','0','+set','vid_win_width','800','+set','vid_win_height','600','+set','vid_width','800','+set','vid_height','600']
 out=base/'stdout.log';fout=out.open('wb');start=time.monotonic()
 env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local');(base/'profile/local').mkdir(parents=True,exist_ok=True)
 proc=subprocess.Popen(args,cwd=base,stdout=fout,stderr=subprocess.STDOUT,env=env)
 result=None;logs={};startup_evidence=[];geometry=None
 try:
  while time.monotonic()-start<45:
   if geometry is None:geometry=window_pixels(proc.pid)
   candidates=list(base.rglob('*.log'))
   if key=='vkquake':candidates.append(Path(os.environ['APPDATA'])/'vkQuake/qconsole.log')
   for p in candidates:
    try: txt=p.read_text(errors='replace')
    except (PermissionError,FileNotFoundError):continue
    try:logname=str(p.relative_to(base))
    except ValueError:logname='native-prefdir/qconsole.log'
    if marker not in txt:continue
    startup_evidence=[line for line in txt.splitlines() if re.search(r'Video mode|GL_VENDOR|GL_RENDERER|GL_VERSION|Initializing vkQuake|Device name|Renderer:|Resolution:|Using SDL',line,re.I)]
    txt=txt[txt.rfind(marker):];logs[logname]=txt
    matches=[m for m in FPS.finditer(txt) if int(m[1])>1000 and float(m[3])>0]
    if matches:
     m=matches[-1];result={'frames':int(m[1]),'seconds':float(m[2]),'fps':float(m[3]),'line':m[0]};break
   if result or proc.poll() is not None:break
   time.sleep(.05)
 finally:
  if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
  fout.close()
 if result and geometry!={'width':800,'height':600}:raise RuntimeError(f'Unexpected physical client size: {key} {geometry}')
 label=('pilot-' if pilot else '')+f'{key}-{mapname}-'+('warmup' if warmup else f'{index:02}')
 logdir=ROOT/'logs';logdir.mkdir(exist_ok=True)
 (logdir/(label+'.log')).write_text('\n'.join(f'--- {path} ---\n{text}' for path,text in logs.items()))
 row={'engine':key,'map':mapname,'run':index,'warmup':warmup,'result':result,'elapsed_wall_seconds':time.monotonic()-start,'command':args,'config':cfg,'startup_evidence':startup_evidence,'window_pixels':geometry,'log':'logs/'+label+'.log'}
 print(label+' '+(result['line'] if result else 'FAILED'),flush=True)
 return row
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--pilot',action='store_true');ap.add_argument('--only');a=ap.parse_args()
 keys=list(NAMES) if not a.only else a.only.split(',')
 for key in keys:prepare(key)
 if a.pilot:
  rows=[run(k,'aerowalk',0,True,True) for k in keys]
  (ROOT/'pilot.json').write_text(json.dumps(rows,indent=2));return
 raw={'method':'Shared protocol-15 NetQuake recordings, stationary 360-degree camera sweep at the first deathmatch spawn; one discarded warm-up then five native timedemo passes per engine/map. 800x600 window, FOV90, VSync/MSAA off, uncapped; renderer defaults otherwise. Fresh process each pass; warmed OS/driver caches.','engines':{k:{'name':NAMES[k],'version':META.get(k,{}).get('release','installed build'),'exe_sha256':hashlib.sha256(SOURCES[k].read_bytes()).hexdigest(),'source':str(SOURCES[k])} for k in keys},'acquisition':META,'demos':json.loads((ROOT/'demos.json').read_text()),'samples':[]}
 path=ROOT/'fps-results.json'
 if path.exists():raw=json.loads(path.read_text())
 done={(x['engine'],x['map'],x['run'],x['warmup']) for x in raw['samples'] if x['result']}
 for mapindex,mapname in enumerate(META['maps']):
  for idx in range(6):
   order=keys[(idx+mapindex)%len(keys):]+keys[:(idx+mapindex)%len(keys)]
   for key in order:
    if (key,mapname,idx,idx==0) in done:continue
    row=run(key,mapname,idx,idx==0);raw['samples'].append(row);path.write_text(json.dumps(raw,indent=2))
    if not row['result']:raise RuntimeError('Failed '+key+' '+mapname+'; inspect retained log')
 print('Complete '+str(len(raw['samples']))+' passes',flush=True)
if __name__=='__main__':main()
