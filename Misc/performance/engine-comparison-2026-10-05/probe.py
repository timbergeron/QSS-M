import os, shutil, subprocess, time
from pathlib import Path

ROOT=Path(__file__).resolve().parent
ENGINES={
 'qssm':Path(r'C:\Users\Tim Bergeron\Desktop\qssm\QSS-M-w64.exe'),
 'fteqw':Path(r'C:\Users\Tim Bergeron\Desktop\Engines\FTE Stuff\FTE\fteqw64.exe')}
PAKS=Path(r'C:\Users\Tim Bergeron\Desktop\qssm\id1')
CONFIG='''vid_width 800
vid_height 600
vid_fullscreen 0
vid_vsync 0
vid_renderer gl
host_maxfps 144
cl_maxfps 144
cl_demoreel 0
cl_discord_presence 0
developer 1
log_developer 1
log_readable 1
cl_loopbackprotocol nqid
sv_protocol 15
sv_protocol_nq 15
'''

def prepare(label):
 base=ROOT/label
 (base/'id1').mkdir(parents=True,exist_ok=True)
 src=ENGINES[label]
 exe=base/src.name
 shutil.copy2(src,exe)
 if label=='qssm':
  for dll in src.parent.glob('*.dll'): shutil.copy2(dll,base/dll.name)
 for name in ('pak0.pak','pak1.pak'):
  dst=base/'id1'/name
  if not dst.exists(): shutil.copyfile(PAKS/name,dst)
 (base/'id1'/'quake.rc').write_text('exec default.cfg\nexec config.cfg\nstuffcmds\n')
 (base/'id1'/'config.cfg').write_text(CONFIG)
 return base,exe

if __name__=='__main__':
 for label in ENGINES:
  base,exe=prepare(label)
  cmd=[str(exe),'-basedir',str(base),'-nohome','-noquakeimport','-condebug','-window','-width','800','-height','600','+developer','1','+map','start','+echo','BENCH_READY']
  out=(base/'stdout.log').open('wb')
  p=subprocess.Popen(cmd,cwd=base,stdout=out,stderr=subprocess.STDOUT)
  time.sleep(7)
  print(label,'running',p.poll() is None,flush=True)
  p.terminate(); p.wait(timeout=10);out.close()
  print('logs',[(str(x.relative_to(base)),x.stat().st_size) for x in base.rglob('*.log')],flush=True)
