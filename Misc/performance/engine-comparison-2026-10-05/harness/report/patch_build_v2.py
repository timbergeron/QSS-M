"""One-off edit of build_report.py: gameplay demos, fps-v2 data, Oct 6 investigation."""
from pathlib import Path

p = Path(__file__).with_name('build_report.py')
t = p.read_text(encoding='utf-8')


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:80]
    t = t.replace(old, new)


rep("""fps = json.loads((HERE / 'fps/fps-results.json').read_text())""",
    """FPSDIR = HERE / ('fps-v2' if (HERE / 'fps-v2/fps-results.json').exists() else 'fps')
fps = json.loads((FPSDIR / 'fps-results.json').read_text())""")

rep("""fps_before_geo = geo(list(fps_before.values()))
""", """fps_before_geo = geo(list(fps_before.values()))

# Gameplay demos: CTF match and the ad_tears speedrun (same fresh-process method).
EXP = HERE.parent / 'fps-expanded-20261006'
workloads, demo_raw = [], {}
for key, title, fname, inst_name, note, na in (
        ('ctf', 'CTF match · ctf3m2', 'ctf-results.json', 'ctf-installed.json',
         '16 players, 51,716 frames, scr_autoid 0', {}),
        ('adtears', 'ad_tears speedrun', 'adtears-results.json', 'adtears-installed.json',
         'Arcane Dimensions 1.80p1, Sphere easy run 2:14, 9,989 frames', {'ezquake': 'No AD or protocol 999 support'})):
    raw = json.loads((EXP / fname).read_text())
    demo_raw[key] = raw
    fstats[key] = {}
    for e in ORDER:
        v = [s['result']['fps'] for s in sorted(raw['samples'], key=lambda s: s['run'])
             if s['engine'] == e and not s['warmup'] and s['result']]
        fstats[key][e] = stats(v) if v else None
        if e not in na:
            assert len(v) == 5, (key, e, len(v))
    ir = json.loads((EXP / inst_name).read_text())
    fps_before[key] = statistics.median(s['result']['fps'] for s in ir['samples'] if not s['warmup'] and s['result'])
    workloads.append({'key': key, 'title': title, 'note': note, 'group': 'demo', 'na': na})
""")

rep("""engines['qssm']['version'] = 'current source (fast loading)'""",
    """engines['qssm']['version'] = 'current source'""")

rep("""builds.insert(1, {'name': 'QSS-M · installed',""",
    """builds.insert(1, {'name': 'QSS-M · current source, FPS runs', 'version': '1.6.9 + loading changes + demo precache', 'renderer': 'OpenGL',
                  'sha': fps['engines']['qssm']['exe_sha256'], 'qssm': True})
builds.insert(2, {'name': 'QSS-M · installed',""")

rep("""for src, dst in ((HERE / 'lifecycle', NEW / 'lifecycle'), (HERE / 'fps', NEW / 'fps'),""",
    """for src, dst in ((HERE / 'lifecycle', NEW / 'lifecycle'), (FPSDIR, NEW / 'fps'),""")

rep("""for f in ('refresh.py', 'build_report.py', 'template.html'):
    shutil.copy2(HERE / f, NEW / f)""",
    """for f in ('refresh.py', 'build_report.py', 'template.html'):
    shutil.copy2(HERE / f, NEW / f)
(NEW / 'gameplay-demos').mkdir(exist_ok=True)
for f in ('ctf-results.json', 'adtears-results.json', 'ctf-installed.json', 'adtears-installed.json', 'demorun.py', 'focusutil.py',
          'demoinfo.py', 'investigation-20261006.json', 'repeat.py', 'cold.py', 'profile_td.py', 'probe-tdframe-stages-glcount.patch',
          'glprobe_count.h', 'swaptest/swaptest.c', 'assets/ctf-assets.json'):
    shutil.copy2(EXP / f, NEW / 'gameplay-demos' / Path(f).name)
shutil.copytree(EXP / 'demoruns/logs', NEW / 'gameplay-demos/logs', dirs_exist_ok=True)
shutil.copytree(EXP / 'logs', NEW / 'gameplay-demos/investigation-logs', dirs_exist_ok=True)""")

rep("""    {'k': 'Nine-map FPS', 'v': f"{geomean['qssm']:,.0f}", 'd': f"Was {fps_before_geo:,.0f} (installed build). Rank {fps_rank} of 6; {engines[fps_leader]['name']} leads at {geomean[fps_leader]:,.0f}."},""",
    """    {'k': 'Nine-map FPS', 'v': f"{geomean['qssm']:,.0f}", 'd': f"Was {fps_before_geo:,.0f} with the installed build (+{(geomean['qssm'] / fps_before_geo - 1) * 100:.0f}%). Rank {fps_rank} of 6; {engines[fps_leader]['name']} leads at {geomean[fps_leader]:,.0f}."},""")

rep("""        'Rendering FPS is where QSS-M still trails.')""",
    """        'Timedemo FPS rose ' + f"{(geomean['qssm'] / fps_before_geo - 1) * 100:.0f}%" + ' after a demo-loading fix, but rendering is where QSS-M still trails.')""")

p.write_text(t, encoding='utf-8')
print('patched')
