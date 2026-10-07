"""Counterbalanced, same-compiler A/B comparison; no runtime control polling."""
import sys, json, shutil, subprocess, time, os, hashlib, statistics, argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parent
FPSROOT=ROOT.parent/'engine-fps-20261005'
sys.path.insert(0,str(FPSROOT)); import measure
ap=argparse.ArgumentParser()
ap.add_argument('--pilot',action='store_true')
ap.add_argument('--maps',default=','.join(json.loads((FPSROOT/'demos.json').read_text())))
args=ap.parse_args()
maps=args.maps.split(',')
session=ROOT/'patch-comparison'/time.strftime('%Y%m%d-%H%M%S')
session.mkdir(parents=True)
source_dirs={'before':ROOT/'bin','after':ROOT/'bin-fast'}
expected_frames={m:next(s['result']['frames'] for s in json.loads((FPSROOT/'fps-results.json').read_text())['samples'] if s['engine']=='qssm' and s['map']==m) for m in maps}
raw={'method':'MSVC Release x64 before/after, same settings/assets/demos. ABBA engine order per resolution; two discarded warmups then three measured timedemos per map/block. Native wait scheduling; no runtime file polling. Full 360-degree recording, native frame counts matched to each original map recording (Aerowalk 1445; other maps 1442). Windowed 800x600 and 3840x2160; VSync/MSAA off, sound on, rendering features/defaults unchanged. No GPU profiler in either binary.',
     'maps':maps,'sources':{},'blocks':[]}
for key,src in source_dirs.items():
    raw['sources'][key]={'exe':str(src/'quakespasm.exe'),'exe_sha256':hashlib.sha256((src/'quakespasm.exe').read_bytes()).hexdigest()}
raw['source_diff']=subprocess.check_output(['git','diff','--','Quake/gl_screen.c'],cwd=ROOT.parents[1],text=True)
(session/'results.json').write_text(json.dumps(raw,indent=2))
conditions=[(800,600)] if args.pilot else [(800,600),(3840,2160)]
order=['before','after'] if args.pilot else ['before','after','after','before']
for width,height in conditions:
    for block_index,key in enumerate(order):
        map_order=maps if block_index<2 else list(reversed(maps))
        base=session/f'{width}x{height}-{block_index}-{key}';base.mkdir()
        for f in source_dirs[key].iterdir():
            if f.is_file() and f.suffix.lower() in ('.exe','.dll','.pdb'):shutil.copy2(f,base/f.name)
        shutil.copytree(FPSROOT/'assets/id1',base/'id1',dirs_exist_ok=True)
        shutil.copytree(FPSROOT/'demos',base/'id1',dirs_exist_ok=True)
        sequence=[(m,i) for m in map_order for i in range(5)]
        cfg=measure.COMMON.replace('vid_width 800',f'vid_width {width}').replace('vid_height 600',f'vid_height {height}')+measure.SPEC['qssm']+f'echo PATCH_BENCH_{key}\n'
        for index,(m,i) in enumerate(sequence):
            cfg+=f'alias patch_{index} "alias patch_{index} wait;echo PATCH_PASS_{m}_{i};timedemo fps_{m}"\n'
        for index in range(len(sequence)):cfg+=f'patch_{index}\n'+'wait\n'*200
        cfg+='echo PATCH_DONE\n'
        rc='exec default.cfg\nexec fpsbench.cfg\n'
        measure.pak(base/'id1/pak2.pak',{'quake.rc':rc,'fpsbench.cfg':cfg})
        (base/'id1/quake.rc').write_text(rc);(base/'id1/fpsbench.cfg').write_text(cfg)
        cmd=[str(base/'quakespasm.exe'),'-basedir',str(base),'-nohome','-noquakeimport','-listen','-noudp','-nojoy','-noice','-condebug','-window','-width',str(width),'-height',str(height)]
        env=os.environ.copy();env['APPDATA']=str(base/'profile');env['LOCALAPPDATA']=str(base/'profile/local')
        data=[];geometry=None;start=time.monotonic()
        print('START',width,height,block_index,key,flush=True)
        with (base/'stdout.log').open('wb') as out:
            p=subprocess.Popen(cmd,cwd=base,env=env,stdout=out,stderr=subprocess.STDOUT)
            try:
                while time.monotonic()-start<240:
                    geometry=measure.window_pixels(p.pid) or geometry
                    log=base/'qconsole.log'
                    if log.exists():
                        text=log.read_text(errors='replace')
                        results=[{'frames':int(m[1]),'seconds':float(m[2]),'fps':float(m[3])} for m in measure.FPS.finditer(text) if int(m[1])>1000]
                        if len(results)>len(data):
                            data=results
                            m,i=sequence[len(data)-1]
                            print('PASS',width,height,key,m,i,data[-1]['fps'],flush=True)
                    if len(data)>=len(sequence) or p.poll() is not None:break
                    time.sleep(.04)
            finally:
                if p.poll() is None:p.terminate();p.wait(timeout=10)
        if geometry!={'width':width,'height':height}:raise RuntimeError(f'Wrong drawable size: {geometry}')
        samples=[dict(x,map=m,pass_index=i,warmup=i<2) for x,(m,i) in zip(data,sequence)]
        if len(data)!=len(sequence) or any(x['frames']!=expected_frames[x['map']] for x in samples):raise RuntimeError(f'Incomplete block: {len(data)} / {len(sequence)}; inspect {base}')
        block={'build':key,'block':block_index,'resolution':[width,height],'samples':samples,'geometry':geometry,'elapsed_seconds':time.monotonic()-start,'command':cmd,'config':cfg,'log':str(base/'qconsole.log')}
        raw['blocks'].append(block);(session/'results.json').write_text(json.dumps(raw,indent=2))
        print('BLOCK_DONE',width,height,key,{m:statistics.median(x['fps'] for x in samples if x['map']==m and not x['warmup']) for m in maps},flush=True)
print('RESULTS',session/'results.json',flush=True)
