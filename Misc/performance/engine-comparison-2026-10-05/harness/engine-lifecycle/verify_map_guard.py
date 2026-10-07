"""Exercise map-command delivery guards without engines or network access."""
import json, tempfile
from pathlib import Path
import remote

class FakeProcess:
 def __init__(self,*args,**kwargs):self.returncode=None
 def poll(self):return self.returncode
 def terminate(self):self.returncode=1
 def wait(self,timeout=None):self.returncode=0;return 0

with tempfile.TemporaryDirectory(prefix='map-guard-',dir=Path(__file__).parent) as temp:
 root=Path(temp);remote.ROOT=root
 remote.time.time_ns=lambda:1234
 remote.subprocess.Popen=FakeProcess
 remote.bench.command=lambda *args:['fake-executable','-ip','127.0.0.1']
 remote.bench.read=lambda path:'BENCH_SETUP_READY_1234'
 base=root/'fteqw'
 for game in ('id1','qw','fte'):(base/game).mkdir(parents=True)
 (base/'fte/qconsole.log').write_text('Synthetic setup log; no engine or network used.')
 remote.bench.prepare=lambda key:(base,base/'fake.exe')

 def checks(counts):
  queue=iter(counts)
  remote.status=lambda ip:next(queue)

 checks([{'players':0,'map':'dm3'},{'players':0,'map':'dm3'},{'players':2,'map':'dm3'}])
 try:remote.restore_map('test-server','test-interface')
 except RuntimeError as error:assert 'Another player joined' in str(error)
 else:raise AssertionError('Map command was not withheld')
 assert all((base/g/'setup-control.cfg').read_text()=='' for g in ('id1','qw'))

 checks([{'players':0,'map':'dm3'},{'players':0,'map':'dm3'},{'players':1,'map':'dm3'},{'players':0,'map':'aerowalk'},{'players':0,'map':'aerowalk'}])
 remote.restore_map('test-server','test-interface')
 assert all('cmd dm normal aerowalk' in (base/g/'setup-control.cfg').read_text() for g in ('id1','qw'))

result={'result':'passed','checks':['Map command withheld when a second player joins during setup','Map command delivered only after signon and the sole setup-client status check'],'network_or_engines_used':False}
out=Path(__file__).resolve().parents[2]/'Misc/performance/engine-comparison-2026-10-05/lifecycle/map-guard-verification.json'
out.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
