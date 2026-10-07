"""Local lifecycle benchmark. No source or installed configs are modified."""
import argparse, hashlib, json, os, platform, re, shutil, socket, struct, subprocess, time
from pathlib import Path
from probe import ROOT, ENGINES, PAKS, prepare

COMMON='''vid_width 800
vid_height 600
vid_fullscreen 0
vid_vsync 0
cl_demoreel 0
developer 1
name BenchClient
'''
SPECIFIC={
 'qssm':'''host_maxfps 144
cl_discord_presence 0
sv_protocol Base-15
r_scale 1
vid_fsaa 0
''',
 'fteqw':'''vid_renderer gl
net_hybriddualstack 0
net_enabled 1
cl_maxfps 144
cl_idlefps 144
cl_loopbackprotocol nqid
cl_nopext 1
sv_protocol_nq 15
vid_multisample 0
log_developer 1
log_readable 1
'''}
WAIT='wait\n'*220

def read(path):
 try:return path.read_text(errors='replace')
 except (FileNotFoundError,PermissionError):return ''

def reset(label):
 base=ROOT/label
 (base/'id1'/'config.cfg').write_text(COMMON+SPECIFIC[label])
 for path in base.rglob('*.log'):path.unlink(missing_ok=True)
 return base,base/ENGINES[label].name,base/('qconsole.log' if label=='qssm' else 'fte/qconsole.log')

def command(base,exe,label):
 args=[str(exe),'-basedir',str(base),'-nohome','-condebug','-window','-width','800','-height','600','-nojoy']
 if label=='qssm':args+=['-noquakeimport']
 return args+['+exec','benchmark.cfg']

def observe(proc,log,timeout=35):
 events=[]; offset=0; start=time.perf_counter()
 while time.perf_counter()-start<timeout:
  text=read(log)
  if len(text)>offset:
   now=time.perf_counter()
   for line in text[offset:].splitlines():
    if 'BENCH_' in line or 'CL_SignonReply: 4' in line:
     events.append({'time':now,'line':line})
   offset=len(text)
  if proc.poll() is not None:return events,time.perf_counter(),proc.returncode
  time.sleep(.001)
 proc.terminate();proc.wait(timeout=10)
 raise TimeoutError('Engine did not finish: '+str(log))

def lifecycle(label,index,port,launch_map=False,connect_only=False):
 base,exe,log=reset(label)
 if launch_map:
  script='echo BENCH_LAUNCH_MAP\nmap start\n'+WAIT+'echo BENCH_QUIT\nquit\n'
 else:
  script='echo BENCH_READY\n'+WAIT+'echo BENCH_BEGIN_initial\nmap start\n'+WAIT+'echo BENCH_BEGIN_change\nmap e1m1\n'+WAIT+'echo BENCH_BEGIN_quit\nquit\n'
 if label=='fteqw':
  if launch_map:
   script='alias bench_quit "echo BENCH_QUIT;quit"\nin 4 bench_quit\necho BENCH_LAUNCH_MAP\nmap start\n'
  else:
   script=f'''alias bench_initial "echo BENCH_BEGIN_initial;map start"
alias bench_change "echo BENCH_BEGIN_change;map e1m1"
alias bench_quit "echo BENCH_BEGIN_quit;quit"
alias bench_schedule "echo BENCH_READY;in 2 bench_initial;in 5 bench_change;in 8 bench_quit"
in 0 bench_schedule
'''
 if connect_only:
  if label=='fteqw':script=f'alias bench_connect "echo BENCH_BEGIN_connect;connectnq 127.0.0.1:{port}"\nalias bench_quit "echo BENCH_BEGIN_quit;quit"\nalias bench_schedule "in 2 bench_connect;in 10 bench_quit"\nin 0 bench_schedule\n'
  else:script=WAIT+f'echo BENCH_BEGIN_connect\nconnect 127.0.0.1:{port}\n'+WAIT+'echo BENCH_BEGIN_quit\nquit\n'
 (base/'id1'/'benchmark.cfg').write_text(script)
 args=command(base,exe,label)
 output=(base/'stdout.log').open('wb')
 launch=time.perf_counter()
 proc=subprocess.Popen(args,cwd=base,stdout=output,stderr=subprocess.STDOUT)
 try:events,exit_time,code=observe(proc,log)
 finally:
  if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
  output.close()
 if code!=0:raise RuntimeError(f'{label} exited with {code}')
 metrics={}; current=None; ready=None;connect_started=None;quit_started=None
 for e in events:
  line=e['line']; t=e['time']
  clean=re.sub(r'^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ', '',line).strip()
  if clean=='BENCH_READY':
   ready=t;metrics['startup']=(t-launch)*1000
  elif clean=='BENCH_LAUNCH_MAP':current='launch_map';begin=launch
  elif re.fullmatch(r'BENCH_BEGIN_\w+',clean):
   current=clean.split('BENCH_BEGIN_')[1];begin=t
   if current=='connect':connect_started=t
   if current=='quit':quit_started=t;metrics['quit']=(exit_time-t)*1000
  elif 'CL_SignonReply: 4' in line and current in ('initial','change','connect','launch_map'):
   metrics[current]=(t-begin)*1000;current=None
 expected={'quit'} if connect_only else {'launch_map'} if launch_map else {'startup','initial','change','quit'}
 if not expected.issubset(metrics):
  raise AssertionError(f'Missing timing endpoints {expected-set(metrics)}: {events}')
 mode='connect' if connect_only else 'launch_map' if launch_map else 'lifecycle'
 archive=ROOT/'logs'/f'{index:02d}-{label}-{mode}.log'
 archive.parent.mkdir(exist_ok=True);shutil.copyfile(log,archive)
 result={'engine':label,'run':index,'mode':mode,'metrics_ms':metrics,'events':[{'elapsed_ms':(e['time']-launch)*1000,'line':e['line']} for e in events],'exit_code':code,'command':args,'log':str(archive.relative_to(ROOT)),'script':script}
 if connect_only:
  result['connect_status']='signon_observed' if 'connect' in metrics else 'signon_not_observed'
  result['connect_observation_window_ms']=(quit_started-connect_started)*1000
  metrics.pop('quit',None)
 print(json.dumps({'engine':label,'run':index,'mode':result['mode'],'metrics_ms':{k:round(v,2) for k,v in metrics.items()}}),flush=True)
 return result

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--runs',type=int,default=5);ap.add_argument('--pilot',action='store_true');ap.add_argument('--engine',choices=list(ENGINES));ap.add_argument('--diagnose-connect',action='store_true');a=ap.parse_args()
 for label in ENGINES:prepare(label)
 server=ROOT/'server';(server/'id1').mkdir(parents=True,exist_ok=True)
 src=ENGINES['qssm'];exe=server/src.name;shutil.copy2(src,exe)
 for dll in src.parent.glob('*.dll'):shutil.copy2(dll,server/dll.name)
 for name in ('pak0.pak','pak1.pak'):
  if not (server/'id1'/name).exists():shutil.copyfile(PAKS/name,server/'id1'/name)
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',26000));port=s.getsockname()[1]
 (server/'id1'/'server.cfg').write_text('sv_public 0\nsv_protocol Base-15\nhostname LocalBenchmark\nmap e1m1\n')
 log=server/'qconsole.log';log.unlink(missing_ok=True)
 server_args=[str(exe),'-basedir',str(server),'-dedicated','4','-nohome','-noquakeimport','-noice','-condebug','-ip','127.0.0.1','-port',str(port),'+rcon_password','benchmark-local-only']
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 p=subprocess.Popen(server_args,cwd=server,creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=startup)
 results=[]
 try:
  end=time.monotonic()+15
  while 'Quake Initialized' not in read(log):
   if time.monotonic()>end or p.poll() is not None:raise RuntimeError('Server failed to start: '+read(log))
   time.sleep(.02)
  time.sleep(.5)
  for index in range(a.runs):
   order=('qssm','fteqw') if index%2==0 else ('fteqw','qssm')
   for label in order:
    if a.engine and label!=a.engine:continue
    if a.diagnose_connect:
     results.append(lifecycle(label,index+1,port,connect_only=True));continue
    results.append(lifecycle(label,index+1,port))
    results.append(lifecycle(label,index+1,port,True))
    results.append(lifecycle(label,index+1,port,connect_only=True))
    time.sleep(.25)
 finally:
  # Send a normal authenticated quit to only our loopback server.
  body=b'\x05benchmark-local-only\0quit\0';packet=struct.pack('>I',0x80000000|(len(body)+4))+body
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.sendto(packet,('127.0.0.1',port))
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=10)
 data={'created_local':time.strftime('%Y-%m-%d %H:%M:%S'),'timezone':'America/Los_Angeles','platform':platform.platform(),'processor':os.environ.get('PROCESSOR_IDENTIFIER','unknown'),'logical_cpus':os.cpu_count(),'poll_interval_ms':1,'server_command':server_args,'engines':{k:{'source':str(v),'sha256':hashlib.sha256(v.read_bytes()).hexdigest(),'size_bytes':v.stat().st_size,'file_modified_epoch':v.stat().st_mtime} for k,v in ENGINES.items()},'pak_sha256':{n:hashlib.sha256((PAKS/n).read_bytes()).hexdigest() for n in ('pak0.pak','pak1.pak')},'config':{k:COMMON+SPECIFIC[k] for k in ENGINES},'results':results}
 (ROOT/('pilot.json' if a.pilot else 'results.json')).write_text(json.dumps(data,indent=2))

if __name__=='__main__':main()
