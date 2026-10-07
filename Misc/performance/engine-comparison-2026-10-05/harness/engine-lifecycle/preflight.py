"""Untimed setup: clear prompts before measuring these stable executable paths."""
import argparse, json, socket, subprocess
from pathlib import Path
import benchmark as b
p=argparse.ArgumentParser();p.add_argument('engine',choices=list(b.SPEC));p.add_argument('--connect',action='store_true');p.add_argument('--set-map');a=p.parse_args()
b.ROOT=Path(__file__).resolve().parent/'remote'
base,exe=b.prepare(a.engine)
import shutil
sound=b.ROOT/'qssm/id1/sound'
if sound.exists() and a.engine!='qssm':shutil.copytree(sound,base/'id1/sound',dirs_exist_ok=True)
for mapname in ('efdm6','fragfes1','aerowalk'):
 source=Path('C:/Users/Tim Bergeron/Desktop/qssm/id1/maps')/(mapname+'.bsp')
 if source.exists():
  (base/'id1/maps').mkdir(exist_ok=True);shutil.copy2(source,base/'id1/maps'/source.name)
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
 s.connect(('70.39.95.254',26000));ip=s.getsockname()[0]
controls=b.COMMON+b.SPEC[a.engine]
if a.set_map:
 assert a.engine=='fteqw' and a.set_map=='aerowalk'
 controls+='alias noop ""\nalias f_spawn "alias f_spawn noop;cmd dm normal aerowalk"\n'
script=('alias preflight_connect "connectnq denver.quakeone.com:26000"\nin 2 preflight_connect\n' if a.engine=='fteqw' else b.WAIT+'connect denver.quakeone.com:26000\n') if a.connect else ''
rc='exec default.cfg\nexec config.cfg\nstuffcmds\n' if a.engine=='fteqw' else 'exec default.cfg\nexec benchmark.cfg\nstuffcmds\n'
files={'quake.rc':rc,'benchmark.cfg':controls+script,'config.cfg':controls,'autoexec.cfg':''}
for game in ('id1','qw'):
 for name,txt in files.items():(base/game/name).write_text(txt)
 b.pak(base/game/'pak2.pak',files)
args=b.command(a.engine,base,exe);args[args.index('-ip')+1]=ip
proc=subprocess.Popen(args,cwd=base)
print(json.dumps({'pid':proc.pid,'executable':str(exe),'purpose':'Untimed prompt preflight; close after setup'}))
