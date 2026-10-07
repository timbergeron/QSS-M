import hashlib,json,shutil,struct,urllib.request,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent
GAME=Path(r'C:\Users\Tim Bergeron\Desktop\qssm\id1')
MAPS=['aerowalk','ztndm3','dm3','bravado','ctf3m2','schloss','e1m2','dm2','ctf2m8']
REPOS={'ezquake':('QW-Group/ezquake-source','ezQuake-windows-x64.zip'), 'ironwail':('andrei-drexler/ironwail','ironwail-0.8.2-win64.zip'), 'vkquake':('Novum/vkQuake','vkQuake-1.36.0_windows_x64.zip')}

def fetch(url):
 return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'Local-Quake-engine-benchmark'}),timeout=60)

def main():
 downloads=ROOT/'downloads';downloads.mkdir(parents=True,exist_ok=True)
 metadata={}
 for label,(repo,asset_name) in REPOS.items():
  d=json.load(fetch('https://api.github.com/repos/'+repo+'/releases/latest'))
  # Pick the platform asset from the freshly resolved stable release.
  assets=[a for a in d['assets'] if (label=='ezquake' and a['name'].lower()=='ezquake-windows-x64.zip') or (label=='ironwail' and a['name'].endswith('-win64.zip')) or (label=='vkquake' and a['name'].endswith('_windows_x64.zip'))]
  assert len(assets)==1,(label,assets)
  a=assets[0];dest=downloads/a['name']
  if not dest.exists():
   with fetch(a['browser_download_url']) as response,dest.open('wb') as out:shutil.copyfileobj(response,out)
  assert dest.stat().st_size==a['size']
  metadata[label]={'release':d['tag_name'],'published_at':d['published_at'],'release_url':d['html_url'],'download_url':a['browser_download_url'],'archive_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'archive':str(dest)}
  print(label,d['tag_name'],dest.name,flush=True)
 url='https://triptohell.info/moodles/qss/quakespasm_spiked_win64_dev.zip'
 dest=downloads/'quakespasm_spiked_win64_dev.zip'
 if not dest.exists():
  with fetch(url) as response,dest.open('wb') as out:shutil.copyfileobj(response,out)
 metadata['qss']={'release':'2024-March-01 (requested archive)','release_url':'https://triptohell.info/moodles/qss/','download_url':url,'archive_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'archive':str(dest)}
 for label,m in metadata.items():
  target=(ROOT/'engines'/label).resolve();target.mkdir(parents=True,exist_ok=True)
  with zipfile.ZipFile(m['archive']) as z:
   for member in z.infolist():
    final=(target/member.filename).resolve()
    assert final.is_relative_to(target),member.filename
   z.extractall(target)
  m['executables']=[str(p) for p in target.rglob('*.exe')]
  print(label,'executables',m['executables'],flush=True)
 assets=ROOT/'assets'/'id1';(assets/'maps').mkdir(parents=True,exist_ok=True)
 for name in ('pak0.pak','pak1.pak'):
  dst=assets/name
  if not dst.exists():shutil.copyfile(GAME/name,dst)
 mapmeta={}
 for name in MAPS:
  loose=GAME/'maps'/(name+'.bsp');src=None
  if loose.exists():data=loose.read_bytes();src=str(loose)
  else:
   preferred={'ctf3m2':'ctflocmaps.pak','ctf2m8':'ctf4maps.pak','dm3':'pak1.pak','dm2':'pak1.pak','e1m2':'pak0.pak'}[name]
   pak=GAME/preferred if (GAME/preferred).exists() else GAME/'paks'/preferred
   with pak.open('rb') as f:
    magic,offset,length=struct.unpack('<4sii',f.read(12));assert magic==b'PACK'
    f.seek(offset);directory=f.read(length)
    for i in range(0,length,64):
     raw,o,s=struct.unpack('<56sii',directory[i:i+64]);entry=raw.split(b'\0')[0].decode()
     if entry=='maps/'+name+'.bsp':f.seek(o);data=f.read(s);src=str(pak)+':'+entry;break
   assert src,name
  (assets/'maps'/(name+'.bsp')).write_bytes(data)
  mapmeta[name]={'source':src,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
 metadata['maps']=mapmeta
 (ROOT/'acquisition.json').write_text(json.dumps(metadata,indent=2))
 print('Nine maps extracted; no external entities, LIT, VIS, textures or replacement models copied.',flush=True)

if __name__=='__main__':main()
