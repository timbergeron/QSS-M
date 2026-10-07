"""Independently reconstruct each timing from its observed native events."""
import csv, hashlib, json, re, statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent
OUT=ROOT.parents[1]/'Misc/performance/engine-comparison-2026-10-05'

def main():
 d=json.loads((OUT/'results.json').read_text());assert len(d['results'])==115
 local=json.loads((ROOT/'results.json').read_text());remote=json.loads((ROOT/'remote/results.json').read_text())
 assert [r for r in d['results'] if r['mode']!='connect_remote']==local['results']
 expected_remote=[dict(r,log='remote/'+r['log'].replace('\\','/')) for r in remote['results']]
 assert [r for r in d['results'] if r['mode']=='connect_remote']==expected_remote
 signatures=set();timings=0;missing=[]
 for r in d['results']:
  sig=(r['engine'],r['run'],r['mode']);assert sig not in signatures;signatures.add(sig)
  assert r['valid'] and (r['exit_code']==0 or r['mode']=='connect_remote') and r['window_pixels']=={'width':800,'height':600}
  if r['mode']=='connect_remote':
   assert r['server_status_before']['players']==0 and r['server_status_before']['map']==r['server_status_after']['map']=='aerowalk'
   assert 'denver.quakeone.com:26000' in r['script']
  events=r['events'];assert all(a['elapsed_ms']<=b['elapsed_ms'] for a,b in zip(events,events[1:]))
  reconstructed={};current=None;begin=0;quit_seen=False
  assert (ROOT/r['log']).read_bytes()==(OUT/'lifecycle'/r['log']).read_bytes()
  log=(ROOT/r['log']).read_text(errors='replace');lines=[re.sub(r'^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ','',x).strip() for x in log.splitlines()]
  marker=next(e['line'] for e in events if e['line'].startswith('BENCH_SESSION_'))
  start=lines.index(marker)
  native=[line for line in lines[start:] if re.fullmatch(r'BENCH_(?:SESSION_\d+|READY|LAUNCH_MAP|BEGIN_\w+|QUIT|SIGNON)',line) or line=='CL_SignonReply: 4']
  assert native==[e['line'] for e in events],sig
  for e in events:
   assert e['line'] in lines
   line=e['line'];t=e['elapsed_ms']
   if line=='BENCH_READY':reconstructed['startup']=t
   elif line=='BENCH_LAUNCH_MAP':current='launch_map';begin=0
   elif line.startswith('BENCH_BEGIN_'):
    current=line[12:];begin=t
    if current=='quit':
     quit_seen=True
     if r['mode']=='lifecycle':reconstructed['quit']=r['exit_elapsed_ms']-t
   elif line=='BENCH_QUIT':quit_seen=True
   elif line=='BENCH_SIGNON' or line=='CL_SignonReply: 4':
    if current in ('initial','change','connect','launch_map'):reconstructed[current]=t-begin;current=None
  if r['mode']=='connect_remote' and 'connect' in reconstructed:reconstructed['connect_remote']=reconstructed.pop('connect')
  assert quit_seen or r['mode']=='connect_remote'
  assert set(reconstructed)==set(r['metrics_ms']),(sig,reconstructed,r['metrics_ms'])
  for key,value in reconstructed.items():assert abs(value-r['metrics_ms'][key])<1e-8 and value>0;timings+=1
  if r['mode']=='connect' and 'connect' not in reconstructed:
   assert r['connect_status']=='signon_not_observed' and r['connect_observation_window_ms']>0;missing.append(sig)
 assert timings==205 and len(missing)==0
 assert len(d['excluded_results'])==20+len(remote.get('excluded_results',[]))
 for r in d['excluded_results']:
  assert r['exclusion_reason'] and (r['engine'] in ('ezquake','fteqw') or r['mode']=='connect_remote')
  assert (ROOT/r['log']).read_bytes()==(OUT/'lifecycle'/r['log']).read_bytes()
  assert all(e['line'] in (ROOT/r['log']).read_text(errors='replace') for e in r['events'])
 html=(OUT/'index.html').read_text(encoding='utf-8')
 data=json.loads(re.search(r'<script type="application/json" id="data">(.*?)</script>',html,re.S)[1])
 assert data['raw']==json.loads((OUT/'results.json').read_text())
 for m in data['metrics']:
  for engine,stats in m['stats'].items():
   samples=[r['metrics_ms'].get(m['key']) for r in sorted(d['results'],key=lambda r:r['run']) if r['engine']==engine and r['mode']==m['mode']]
   if not samples:assert engine=='ezquake' and m['key']=='connect_remote';samples=[None]*5
   valid=[s for s in samples if s is not None];assert samples==stats['samples']
   assert stats['median']==(statistics.median(valid) if valid else None)
   assert stats['min']==(min(valid) if valid else None) and stats['max']==(max(valid) if valid else None)
 with (OUT/'samples.csv').open(newline='') as f:rows=list(csv.DictReader(f))
 assert len(rows)==30
 for row in rows:
  engine=next(k for k,e in data['raw']['engines'].items() if e['name']==row['engine']);i=int(row['run'])-1
  for m in data['metrics']:
   value=m['stats'][engine]['samples'][i];cell=row[m['key']+'_ms']
   assert cell=='' if value is None else abs(float(cell)-value)<.000001
 fps=json.loads(re.search(r'<script type="application/json" id="fps-data">(.*?)</script>',html,re.S)[1])
 assert fps['raw']==json.loads((OUT/'fps-results.json').read_text())
 assert len(fps['raw']['samples'])==324 and len(fps['stats'])==9 and len(fps['engines'])==6
 for resource in remote['cached_resources']:
  for key in ('qssm','fteqw','ironwail','vkquake','qss'):
   assert hashlib.sha256((ROOT/'remote'/key/'id1'/resource['path']).read_bytes()).hexdigest()==resource['sha256']
 for key in ('qssm','fteqw','ironwail','vkquake','qss'):
  assert hashlib.sha256((ROOT/'remote'/key/'id1/maps/aerowalk.bsp').read_bytes()).hexdigest()==remote['bsp']['sha256']
 verification={'client_launches':115,'local_normal_exits':90,'denver_launches':25,'reconstructed_positive_timings':timings,'unavailable_connect_attempts':len(missing),'inapplicable_denver_engine':'ezQuake (QuakeWorld only)','native_logs_verified':115,'excluded_attempts_verified':len(d['excluded_results']),'denver_map':'aerowalk','denver_empty_before_every_attempt':True,'identical_custom_resources':len(remote['cached_resources']),'physical_client_area':'800x600 in every session','csv_rows':30,'fps_passes_preserved':324,'fps_results_sha256':hashlib.sha256((OUT/'fps-results.json').read_bytes()).hexdigest(),'result':'passed'}
 (OUT/'lifecycle/data-verification.json').write_text(json.dumps(verification,indent=2))
 print(json.dumps(verification,indent=2))

if __name__=='__main__':main()
