"""Six-engine portable lifecycle measurements; only owned processes are stopped."""
import argparse, csv, hashlib, json, os, platform, re, shutil, socket, statistics, struct, subprocess, sys, time
from pathlib import Path

ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/('engine-fps-20261005' if (ROOT.parent/'engine-fps-20261005').exists() else 'fps')
sys.path.insert(0,str(FPSROOT))
from measure import SOURCES, NAMES, pak, window_pixels
COMMON='vid_width 800\nvid_height 600\nvid_fullscreen 0\nvid_vsync 0\ndeveloper 1\nname BenchClient\nfov 90\nviewsize 100\ndeathmatch 0\ncoop 0\n'
SPEC={
 'qssm':'host_maxfps 144\nsys_throttle -1\ncl_demoreel 0\ncl_discord_presence 0\nsv_protocol Base-15\nr_scale 1\nvid_fsaa 0\n',
 'qss':'host_maxfps 144\nsys_throttle -1\nsv_protocol Base-15\nr_scale 1\nvid_fsaa 0\n',
 'ironwail':'host_maxfps 144\nsv_protocol 15\nr_scale 1\nvid_fsaa 0\n',
 'vkquake':'host_maxfps 144\nsv_protocol Base-15\nr_scale 1\nvid_fsaa 0\n',
 'fteqw':'vid_renderer gl\ncl_maxfps 144\ncl_idlefps 144\ncl_loopbackprotocol nqid\ncl_nopext 1\nsv_protocol_nq 15\nvid_multisample 0\nlog_developer 1\nlog_readable 1\n',
 'ezquake':'cl_maxfps 144\ncl_physfps 77\nsys_inactivesleep 0\nvid_win_width 800\nvid_win_height 600\nvid_framebuffer_scale 1\nsv_forcenqprogs 1\nmaxclients 1\ndeathmatch 0\nalias f_spawn "echo BENCH_SIGNON"\n'}
WAIT='wait\n'*220

def read(p):
 try:return p.read_text(errors='replace')
 except (OSError,UnicodeError):return ''

def prepare(key,role=None):
 base=ROOT/(role or key);base.mkdir(parents=True,exist_ok=True);src=SOURCES[key]
 if key in ('qssm','fteqw'):
  shutil.copy2(src,base/src.name)
  for f in src.parent.glob('*.dll'):shutil.copy2(f,base/f.name)
 else:shutil.copytree(src.parent,base,dirs_exist_ok=True)
 for game in ('id1','qw'):(base/game).mkdir(exist_ok=True)
 for name in ('pak0.pak','pak1.pak'):
  dest=base/'id1'/name
  if not dest.exists():shutil.copyfile(FPSROOT/'assets/id1'/name,dest)
 return base,base/src.name

def command(key,base,exe):
 args=[str(exe),'-basedir',str(base),'-nohome','-condebug','-window','-width','800','-height','600','-nojoy','-noice','-ip','127.0.0.1']
 if key=='qssm':args+=['-noquakeimport']
 if key in ('qssm','qss','ironwail','vkquake'):args+=['-listen','8','-port','26002']
 if key=='fteqw':args+=['-noupdates','+exec','benchmark.cfg']
 if key=='ezquake':args+=['+cl_verify_qwprotocol','0','+set','vid_win_width','800','+set','vid_win_height','600','+set','vid_width','800','+set','vid_height','600']
 return args

def session(key,index,mode,ports,pilot=False,remote_target=None,local_ip=None):
 base=ROOT/key;exe=base/SOURCES[key].name
 for p in base.rglob('*.log'):p.unlink(missing_ok=True)
 marker='BENCH_SESSION_'+str(time.time_ns())
 if mode=='lifecycle':script='echo BENCH_READY\n'+WAIT+'echo BENCH_BEGIN_initial\nmap start\n'+WAIT+'echo BENCH_BEGIN_change\nmap e1m1\n'+WAIT+'echo BENCH_BEGIN_quit\nquit\n'
 elif mode=='launch_map':script='echo BENCH_LAUNCH_MAP\nmap start\n'+WAIT+'echo BENCH_QUIT\nquit\n'
 else:
  target=remote_target or f'127.0.0.1:{ports["qw" if key=="ezquake" else "nq"]}'
  script=WAIT+f'echo BENCH_BEGIN_connect\nconnect {target}\n'+WAIT+'echo BENCH_QUIT\nquit\n'
 if key in ('qssm','qss','ironwail','vkquake'):
  # Native console quit bypasses game-menu confirmation on FitzQuake branches.
  script=script.replace('echo BENCH_BEGIN_quit\nquit','toggleconsole\necho BENCH_BEGIN_quit\nquit').replace('echo BENCH_QUIT\nquit','toggleconsole\necho BENCH_QUIT\nquit')
 if key=='fteqw':
  if mode=='lifecycle':script='alias bench_initial "echo BENCH_BEGIN_initial;map start"\nalias bench_change "echo BENCH_BEGIN_change;map e1m1"\nalias bench_quit "echo BENCH_BEGIN_quit;quit"\nalias bench_schedule "echo BENCH_READY;in 2 bench_initial;in 5 bench_change;in 8 bench_quit"\nin 0 bench_schedule\n'
  elif mode=='launch_map':script='alias bench_quit "echo BENCH_QUIT;quit"\nin 4 bench_quit\necho BENCH_LAUNCH_MAP\nmap start\n'
  else:script=f'alias bench_connect "echo BENCH_BEGIN_connect;connectnq {target}"\nalias bench_quit "echo BENCH_QUIT;quit"\nalias bench_schedule "in 2 bench_connect;in 10 bench_quit"\nin 0 bench_schedule\n'
 controls=COMMON+SPEC[key]
 if remote_target:
  if key in ('qssm','qss','vkquake'):controls+='cl_nopext 1\n'
  if key=='fteqw':controls+='alias bench_remote_quit "echo BENCH_QUIT;disconnect;quit"\nalias f_spawn "in 0.3 bench_remote_quit"\n'
  # Leave the main command buffer empty during the public server handshake.
  # Stop only this owned client after readiness or a bounded observation.
  script=(f'alias bench_connect "echo BENCH_BEGIN_connect;connectnq {target}"\nin 2 bench_connect\n' if key=='fteqw' else WAIT+f'echo BENCH_BEGIN_connect\nconnect {target}\n')
 cfg=controls+f'echo {marker}\n'+script
 if key=='ezquake':
  # Host_Init flushes the main buffer ignoring waits. The native server buffer
  # is not flushed and leaves f_spawn's main-buffer notification unblocked.
  shortwait='wait\n'*80
  if mode=='lifecycle':script=shortwait+'alias f_spawn "echo BENCH_SIGNON;serverexec change.cfg"\necho BENCH_BEGIN_initial\nmap start\n'
  elif mode=='launch_map':script='alias f_spawn "echo BENCH_SIGNON;serverexec quit.cfg"\necho BENCH_LAUNCH_MAP\nmap start\n'
  else:script=shortwait+f'alias f_spawn "echo BENCH_SIGNON;serverexec quit.cfg"\necho BENCH_BEGIN_connect\nconnect 127.0.0.1:{ports["qw"]}\n'
  script=('echo BENCH_READY\n' if mode=='lifecycle' else '')+script
  cfg=controls+f'echo {marker}\n'+'serverexec actions.cfg\n'
 rc='exec default.cfg\nexec benchmark.cfg\n' if key!='fteqw' else 'exec default.cfg\nexec config.cfg\nstuffcmds\n'
 for game in ('id1','qw'):
  files={'quake.rc':rc,'benchmark.cfg':cfg,'actions.cfg':script,'config.cfg':controls,'autoexec.cfg':'exec benchmark.cfg\n' if key=='ezquake' else ''}
  if remote_target and key=='qssm':files['connect.cfg']='disconnect\ntoggleconsole\necho BENCH_QUIT\nquit\n'
  if key=='ezquake':files.update({'change.cfg':'wait\n'*80+'alias f_spawn "echo BENCH_SIGNON;serverexec quit.cfg"\necho BENCH_BEGIN_change\nmap e1m1\n','quit.cfg':'wait\n'*80+'echo BENCH_BEGIN_quit\nquit\n'})
  for name,txt in files.items():(base/game/name).write_text(txt)
  pak(base/game/'pak2.pak',files)
 args=command(key,base,exe)
 if local_ip:args[args.index('-ip')+1]=local_ip
 candidates=[base/p for p in ('qconsole.log','id1/qconsole.log','fte/qconsole.log','qw/qconsole.log')]
 if key=='vkquake':candidates.append(Path(os.environ['APPDATA'])/'vkQuake/qconsole.log')
 env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local');(base/'profile/local').mkdir(parents=True,exist_ok=True)
 fout=(base/'stdout.log').open('wb');events=[];chosen=None;offset=0;geometry=None
 launch=time.perf_counter();p=subprocess.Popen(args,cwd=base,stdout=fout,stderr=subprocess.STDOUT,env=env)
 try:
  while time.perf_counter()-launch<35:
   if chosen is None:
    for path in candidates:
     txt=read(path)
     if marker in txt:chosen=path;offset=txt.find(marker);break
   if chosen:
    txt=read(chosen)
    if len(txt)>offset:
     now=time.perf_counter()
     for line in txt[offset:].splitlines():
      clean=re.sub(r'^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ','',line).strip()
      if re.fullmatch(r'BENCH_(?:SESSION_\d+|READY|LAUNCH_MAP|BEGIN_\w+|QUIT|SIGNON)',clean) or 'CL_SignonReply: 4' in clean:
       events.append({'elapsed_ms':(now-launch)*1000,'line':clean})
     offset=len(txt)
     if geometry is None:geometry=window_pixels(p.pid)
   if remote_target and key not in ('fteqw','qssm') and any('CL_SignonReply: 4' in e['line'] for e in events):break
   if p.poll() is not None:break
   time.sleep(.001)
  if p.poll() is None and not remote_target:raise TimeoutError(f'{key} {mode} timed out')
  exited=(time.perf_counter()-launch)*1000
 finally:
  cleanup_endpoints=[]
  if p.poll() is None:
   if remote_target:
    from udp_cleanup import owned_ipv4_endpoints,disconnect
    cleanup_endpoints=owned_ipv4_endpoints(p.pid)
   p.terminate();p.wait(timeout=10)
   if remote_target:cleanup_endpoints=disconnect(cleanup_endpoints,remote_target,local_ip)
  fout.close()
 log=read(chosen) if chosen else read(base/'stdout.log')
 archive=ROOT/('pilot-logs' if pilot else 'logs')/f'{index:02}-{key}-{mode}.log';archive.parent.mkdir(exist_ok=True);archive.write_bytes((chosen if chosen else base/'stdout.log').read_bytes())
 metrics={};current=None;begin=None;quit_at=None;connect_at=None
 for e in events:
  line=e['line'];t=e['elapsed_ms']
  if line=='BENCH_READY':metrics['startup']=t
  elif line=='BENCH_LAUNCH_MAP':current='launch_map';begin=0
  elif line.startswith('BENCH_BEGIN_'):
   current=line.removeprefix('BENCH_BEGIN_');begin=t
   if current=='connect':connect_at=t
   if current=='quit':quit_at=t;metrics['quit']=exited-t
  elif line=='BENCH_QUIT':quit_at=t
  elif line=='BENCH_SIGNON' or 'CL_SignonReply: 4' in line:
   if current in ('initial','change','connect','launch_map'):metrics[current]=t-begin;current=None
 row={'engine':key,'run':index,'mode':mode,'metrics_ms':metrics,'events':events,'exit_elapsed_ms':exited,'exit_code':p.returncode,'command':args,'script':cfg,'actions_script':script,'auxiliary_scripts':{name:txt for name,txt in files.items() if name in ('change.cfg','quit.cfg','connect.cfg')},'config':controls,'window_pixels':geometry,'log':str(archive.relative_to(ROOT)),'endpoint':'f_spawn after CL_MakeActive' if key=='ezquake' else 'CL_SignonReply: 4'}
 if mode=='connect':row.update(connect_status='signon_observed' if 'connect' in metrics else 'signon_not_observed',connect_observation_window_ms=(quit_at-connect_at) if quit_at and connect_at else None,server='qw' if key=='ezquake' else 'nq')
 expected={'startup','initial','change','quit'} if mode=='lifecycle' else {'launch_map'} if mode=='launch_map' else set()
 if mode!='lifecycle':metrics.pop('quit',None)
 row['valid']=p.returncode==0 and expected.issubset(metrics) and geometry=={'width':800,'height':600} and quit_at is not None
 if remote_target:
  row.update(cleanup='Native post-signon callback disconnected and quit; no quit timing measured' if p.returncode==0 else 'Owned process terminated after signon observation or 35-second launch deadline; no quit timing measured',connect_observation_window_ms=next((e['elapsed_ms']-connect_at for e in events if 'CL_SignonReply: 4' in e['line']),exited-connect_at) if connect_at else None)
  row['valid']=geometry=={'width':800,'height':600} and connect_at is not None
  row['disconnect_cleanup_endpoints']=cleanup_endpoints
 print(json.dumps({'engine':key,'run':index,'mode':mode,'valid':row['valid'],'metrics_ms':{k:round(v,2) for k,v in metrics.items()},'exit':p.returncode,'geometry':geometry}),flush=True)
 return row

def start_servers():
 servers={};ports={'nq':26001,'qw':27501}
 # Check both server ports before launching either owned process.
 for port in ports.values():
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',port))
 for proto,key in [('nq','qssm'),('qw','fteqw')]:
  base,exe=prepare(key,'server-'+proto)
  for port in [ports[proto]]:
   with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',port))
  cfg='sv_public 0\nhostname LocalBenchmark\nrcon_password benchmark-local-only\n'+('sv_protocol Base-15\n' if proto=='nq' else 'sv_protocol_nq 15\nmaxclients 4\ndeathmatch 0\n')+'map e1m1\n'
  for game in ('id1','qw'):
   (base/game/'server.cfg').write_text(cfg);(base/game/'autoexec.cfg').write_text('')
  args=[str(exe),'-basedir',str(base),'-dedicated','4','-nohome','-condebug','-noice','-ip','127.0.0.1','-port',str(ports[proto])]
  if proto=='nq':args+=['-noquakeimport']
  else:args+=['+sv_public','0','+exec','server.cfg']
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  log=base/('qconsole.log' if proto=='nq' else 'fte/qconsole.log');log.parent.mkdir(exist_ok=True);log.unlink(missing_ok=True)
  p=subprocess.Popen(args,cwd=base,creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=startup)
  servers[proto]={'process':p,'command':args,'config':cfg,'log':log,'port':ports[proto]}
  time.sleep(1)
  if p.poll() is not None:
   stop_servers(servers)
   raise RuntimeError(f'Server {proto} exited: '+read(log))
 return servers,ports

def stop_servers(servers):
 for proto,srv in servers.items():
  body=b'\x05benchmark-local-only\0quit\0' if proto=='nq' else b'\xff\xff\xff\xffrcon benchmark-local-only quit\n'
  packet=struct.pack('>I',0x80000000|(len(body)+4))+body if proto=='nq' else body
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.sendto(packet,('127.0.0.1',srv['port']))
  try:srv['process'].wait(timeout=5)
  except subprocess.TimeoutExpired:srv['process'].terminate();srv['process'].wait(timeout=10)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--pilot',action='store_true');ap.add_argument('--only');ap.add_argument('--modes',default='lifecycle,launch_map,connect');ap.add_argument('--replace',action='store_true');ap.add_argument('--reason',default='ezQuake startup marker preceded deferred renderer configuration; replaced after endpoint review');a=ap.parse_args()
 keys=a.only.split(',') if a.only else list(NAMES)
 for key in keys:prepare(key)
 servers,ports=start_servers() if 'connect' in a.modes.split(',') else ({},{'nq':26001,'qw':27501})
 path=ROOT/('pilot.json' if a.pilot else 'results.json')
 raw={'created_local':time.strftime('%Y-%m-%d %H:%M:%S'),'timezone':'America/Los_Angeles','platform':platform.platform(),'logical_cpus':os.cpu_count(),'poll_interval_ms':1,'engines':{k:{'name':NAMES[k],'source':str(SOURCES[k]),'sha256':hashlib.sha256(SOURCES[k].read_bytes()).hexdigest(),'version':json.loads((FPSROOT/'fps-results.json').read_text())['engines'][k]['version']} for k in NAMES},'pak_sha256':{n:hashlib.sha256((FPSROOT/'assets/id1'/n).read_bytes()).hexdigest() for n in ('pak0.pak','pak1.pak')},'config':{k:COMMON+SPEC[k] for k in NAMES},'servers':{k:{x:v for x,v in s.items() if x not in ('process','log')} for k,s in servers.items()},'results':[]}
 if path.exists() and not a.pilot:raw=json.loads(path.read_text())
 if a.replace:
  excluded=[r for r in raw['results'] if r['engine'] in keys and r['mode'] in a.modes.split(',')]
  archive=ROOT/'excluded-logs'/str(time.time_ns());archive.mkdir(parents=True,exist_ok=True)
  for r in excluded:
   old=ROOT/r['log'];dest=archive/old.name;shutil.copy2(old,dest);r['log']=str(dest.relative_to(ROOT));r['exclusion_reason']=a.reason
  raw.setdefault('excluded_results',[]).extend(excluded)
  raw['results']=[r for r in raw['results'] if r not in excluded]
  raw['collection_notes']=raw.get('collection_notes','')+' Replacement batch: '+a.reason+'. Originals retained in excluded_results; batch measured separately.'
  if servers:
   for proto in servers:
    previous=ROOT/f'server-{proto}.log'
    if previous.exists():
     destination=archive/previous.name;shutil.copy2(previous,destination)
     raw.setdefault('archived_server_logs',[]).append(str(destination.relative_to(ROOT)))
 done={(r['engine'],r['run'],r['mode']) for r in raw['results'] if r['valid']}
 try:
  for index in range(1,2 if a.pilot else 6):
   order=keys[(index-1)%len(keys):]+keys[:(index-1)%len(keys)]
   for key in order:
    for mode in a.modes.split(','):
     if (key,index,mode) in done:continue
     row=session(key,index,mode,ports,a.pilot);raw['results'].append(row);path.write_text(json.dumps(raw,indent=2))
     if not row['valid']:raise RuntimeError(f'Invalid {key} {mode}; inspect archived log')
 finally:
  stop_servers(servers)
  for proto,s in servers.items():shutil.copyfile(s['log'],ROOT/f'server-{proto}.log')
 print('Completed '+str(len(raw['results']))+' sessions',flush=True)

if __name__=='__main__':main()
