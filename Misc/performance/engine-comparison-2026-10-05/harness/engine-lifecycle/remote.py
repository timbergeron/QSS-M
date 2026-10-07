"""Fresh-menu connections to the user-requested public Denver server."""
import argparse, hashlib, json, shutil, socket, struct, time, subprocess
from pathlib import Path
import benchmark as bench
ROOT=Path(__file__).resolve().parent/'remote'
HOST='denver.quakeone.com';PORT=26000
KEYS=['qssm','fteqw','ironwail','vkquake','qss']

def empty_server(ip):
 deadline=time.monotonic()+90
 while status(ip)['players']:
  if time.monotonic()>deadline:raise RuntimeError('Denver is occupied; retain batch and do not measure against player activity')
  time.sleep(2)

def restore_map(ip,local_ip):
 empty_server(ip)
 if status(ip)['map']=='aerowalk':return
 base,exe=bench.prepare('fteqw')
 marker='BENCH_SETUP_READY_'+str(time.time_ns())
 cfg=bench.COMMON+bench.SPEC['fteqw']+f'alias noop ""\nalias setup_done "disconnect;quit"\nalias setup_poll "exec setup-control.cfg;in 0.1 setup_poll"\nalias f_spawn "alias f_spawn noop;echo {marker};in 0.1 setup_poll"\nalias setup_connect "connectnq denver.quakeone.com:26000"\nin 2 setup_connect\n'
 files={'quake.rc':'exec default.cfg\nexec config.cfg\nstuffcmds\n','config.cfg':cfg.split('alias noop')[0],'benchmark.cfg':cfg,'autoexec.cfg':''}
 for game in ('id1','qw'):
  for name,value in files.items():(base/game/name).write_text(value)
  (base/game/'setup-control.cfg').write_text('')
  bench.pak(base/game/'pak2.pak',files)
 args=bench.command('fteqw',base,exe);args[args.index('-ip')+1]=local_ip
 proc=subprocess.Popen(args,cwd=base,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  deadline=time.monotonic()+35;authorized=False
  while proc.poll() is None and time.monotonic()<deadline:
   if marker in bench.read(base/'fte/qconsole.log'):
    guard=status(ip)
    if guard['players']!=1:raise RuntimeError('Another player joined during setup; map command cancelled')
    # Deliver only after the setup client has joined and this final status check.
    for game in ('id1','qw'):
     (base/game/'setup-control.cfg').write_text('alias setup_poll noop\ncmd dm normal aerowalk\nin 2 setup_done\n')
    authorized=True;break
   time.sleep(.05)
  assert authorized,'Setup client did not reach readiness; map command withheld'
  proc.wait(timeout=10)
 finally:
  if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
 destination=ROOT/'setup-logs'/f'{time.time_ns()}-aerowalk.log';destination.parent.mkdir(exist_ok=True)
 shutil.copy2(base/'fte/qconsole.log',destination)
 empty_server(ip);assert status(ip)['map']=='aerowalk','Authorized map command did not take effect'
 print('Restored Denver to aerowalk (untimed setup)',flush=True)

def status(ip):
 body=b'\x02QUAKE\0\x03';packet=struct.pack('>I',0x80000000|(len(body)+4))+body
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
  s.settimeout(5);s.sendto(packet,(ip,PORT));data,addr=s.recvfrom(4096)
 parts=data[5:].split(b'\0',3);tail=parts[3]
 return {'queried_local':time.strftime('%Y-%m-%d %H:%M:%S'),'address':parts[0].decode(errors='replace'),'name':parts[1].decode(errors='replace'),'map':parts[2].decode(errors='replace'),'players':tail[0],'maxplayers':tail[1],'response_hex':data.hex(),'responded_from':list(addr)}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--pilot',action='store_true');ap.add_argument('--only');a=ap.parse_args()
 ROOT.mkdir(exist_ok=True);ip=socket.gethostbyname(HOST)
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.connect((ip,PORT));local_ip=s.getsockname()[0]
 server=status(ip);mapname='aerowalk';source=Path('C:/Users/Tim Bergeron/Desktop/qssm/id1/maps')/(mapname+'.bsp');assert source.exists(),source
 bench.ROOT=ROOT;keys=a.only.split(',') if a.only else KEYS
 for key in keys:
  base,exe=bench.prepare(key);(base/'id1/maps').mkdir(exist_ok=True);shutil.copy2(source,base/'id1/maps'/source.name)
  if key!='qssm':shutil.copytree(ROOT/'qssm/id1/sound',base/'id1/sound',dirs_exist_ok=True)
 resources=[{'path':str(p.relative_to(ROOT/'qssm/id1')).replace('\\','/'),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in sorted((ROOT/'qssm/id1/sound').rglob('*')) if p.is_file()]
 raw={'target':f'{HOST}:{PORT}','resolved_ipv4':ip,'local_interface_ipv4':local_ip,'timezone':'America/Los_Angeles','initial_server_status':server,'bsp':{'map':mapname,'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'bytes':source.stat().st_size},'cached_resources':resources,'method':'Fresh process, menu settle, hostname connect command to native signon stage 4. Same preinstalled BSP and custom sound bytes, warm OS/DNS caches. Protocol extensions disabled where supported: base NetQuake/FitzQuake networking. Real public network/server behavior and native retries included. No player chat or input. Five rounds with rotated engine order. User-authorized cmd dm normal aerowalk fixed the map before collection. Player count must return to zero before every launch; map status checked before and after. Native post-signon quit callbacks in QSS-M and FTE, owned-process termination otherwise; cleanup is excluded from connect timing and no public-session quit metric is measured. ezQuake omitted because the server is NetQuake and ezQuake live networking is QuakeWorld.','results':[]}
 method=raw['method'];path=ROOT/('pilot.json' if a.pilot else 'results.json')
 if path.exists() and not a.pilot:raw=json.loads(path.read_text())
 if not a.pilot:
  excluded=[r for r in raw['results'] if r['server_status_before']['map']!='aerowalk' or r['server_status_after']['map']!='aerowalk']
  for r in excluded:
   old=ROOT/r['log'];dest=ROOT/'excluded-logs'/f'{time.time_ns()}-{old.name}';dest.parent.mkdir(exist_ok=True);shutil.copy2(old,dest)
   r['log']=str(dest.relative_to(ROOT));r['exclusion_reason']='Server map rotated away from user-selected aerowalk; excluded from fixed-map summaries'
  raw.setdefault('excluded_results',[]).extend(excluded);raw['results']=[r for r in raw['results'] if r not in excluded]
  raw['method']=method+' On resumption, map identity is fixed to aerowalk rather than adopting the current rotation. Affected rotated-map attempts are excluded. Owned source-port disconnect datagrams are used after termination when needed; endpoint ownership is retained per row.'
  path.write_text(json.dumps(raw,indent=2))
 done={(r['engine'],r['run']) for r in raw['results'] if r['valid']}
 for index in range(1,2 if a.pilot else 6):
  order=keys[(index-1)%len(keys):]+keys[:(index-1)%len(keys)]
  for key in order:
   if (key,index) in done:continue
   empty_server(ip);restore_map(ip,local_ip)
   before=status(ip);assert before['map']==mapname,'Server changed map; retain samples separately'
   row=bench.session(key,index,'connect',{'nq':PORT,'qw':27501},pilot=a.pilot,remote_target=f'{HOST}:{PORT}',local_ip=local_ip)
   row['server_status_before']=before;row['server_status_after']=status(ip);row['server']='denver';row['mode']='connect_remote'
   row['valid']=row['valid'] and row['server_status_after']['map']==mapname
   if 'connect' in row['metrics_ms']:row['metrics_ms']['connect_remote']=row['metrics_ms'].pop('connect')
   raw['results'].append(row);path.write_text(json.dumps(raw,indent=2))
   if not row['valid']:raise RuntimeError(f'Invalid {key} Denver connection; inspect retained logs')
 print('Completed '+str(len(raw['results']))+' Denver sessions',flush=True)

if __name__=='__main__':main()
