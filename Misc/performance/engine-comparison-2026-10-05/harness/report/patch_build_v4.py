"""One-off edit of build_report.py: QSS-M rows from the reviewed (fix1) build, one revision everywhere."""
from pathlib import Path

p = Path(__file__).with_name('build_report.py')
t = p.read_text(encoding='utf-8')


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:80]
    t = t.replace(old, new)


# Lifecycle: QSS-M rows from the fix1 rerun.
rep("""life = json.loads((HERE / 'lifecycle/results.json').read_text())
assert all(r['valid'] for r in life['results']), 'invalid lifecycle rows'""",
    """life = json.loads((HERE / 'lifecycle/results.json').read_text())
assert all(r['valid'] for r in life['results']), 'invalid lifecycle rows'
# QSS-M itself is re-measured with the reviewed build (same day, same harness);
# the other engines keep their rows from the full six-engine run.
life_q = json.loads((HERE / 'lifecycle-fix1/results.json').read_text())
assert all(r['valid'] for r in life_q['results'])
life['results'] = [r for r in life['results'] if r['engine'] != 'qssm'] + life_q['results']
life['engines']['qssm'] = life_q['engines']['qssm']""")

# FPS: QSS-M rows from the fix1 rerun.
rep("""fps = json.loads((FPSDIR / 'fps-results.json').read_text())""",
    """fps = json.loads((FPSDIR / 'fps-results.json').read_text())
fps_q = json.loads((HERE / 'fps-fix1/fps-results.json').read_text())
fps['samples'] = [s for s in fps['samples'] if s['engine'] != 'qssm'] + fps_q['samples']
fps['engines']['qssm'] = fps_q['engines']['qssm']""")

# Gameplay demos: QSS-M rows from the fix1 rerun.
rep("""    raw = json.loads((EXP / fname).read_text())
    demo_raw[key] = raw""",
    """    raw = json.loads((EXP / fname).read_text())
    rq = json.loads((EXP / fname.replace('-results', '-fix1')).read_text())
    raw['samples'] = [s for s in raw['samples'] if s['engine'] != 'qssm'] + rq['samples']
    raw['engines']['qssm'] = rq['engines']['qssm']
    demo_raw[key] = raw""")

# Builds: one QSS-M revision for lifecycle, FPS and demos.
rep("""builds.insert(1, {'name': 'QSS-M · current source, FPS runs', 'version': '1.6.9 + loading changes + demo precache', 'renderer': 'OpenGL',
                  'sha': fps['engines']['qssm']['exe_sha256'], 'qssm': True})
builds.insert(2, {'name': 'QSS-M · installed',""",
    """builds.insert(1, {'name': 'QSS-M · map-loading head-to-head', 'version': 'loading changes before review fixes', 'renderer': 'OpenGL',
                  'sha': loading['candidate_sha256'], 'qssm': True})
builds.insert(2, {'name': 'QSS-M · installed',""")
rep("""assert life['engines']['qssm']['sha256'] == loading['candidate_sha256'], 'lifecycle rerun must use the measured loading build'""",
    """QSHA = life['engines']['qssm']['sha256']
assert fps['engines']['qssm']['exe_sha256'] == QSHA, 'lifecycle and FPS must use the same QSS-M build'
assert all(raw['engines']['qssm']['sha256'] == QSHA for raw in demo_raw.values()), 'gameplay demos must use the same QSS-M build'
builds[0]['version'] = '1.6.9 + uncommitted changes (source.patch)'""")

# Raw data: copy the fix1 reruns, the matching source patch and a test build.
rep("""(NEW / 'gameplay-demos').mkdir(exist_ok=True)""",
    """for src, dst in ((HERE / 'lifecycle-fix1', NEW / 'lifecycle-qssm-current'), (HERE / 'fps-fix1', NEW / 'fps-qssm-current')):
    dst.mkdir(exist_ok=True)
    for name in ('results.json', 'fps-results.json'):
        if (src / name).exists():
            shutil.copy2(src / name, dst / name)
    if (src / 'logs').exists():
        shutil.copytree(src / 'logs', dst / 'logs', dirs_exist_ok=True)
import subprocess, zipfile
patch = subprocess.check_output(['git', 'diff', '--', 'Quake'], cwd=REPO)
(NEW / 'qssm-source.patch').write_bytes(patch)
BIN = HERE.parent / 'map-load-finish-20261006/bin-fix1'
assert hashlib.sha256((BIN / 'quakespasm.exe').read_bytes()).hexdigest() == QSHA, 'packaged build must be the measured build'
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
with zipfile.ZipFile(NEW / 'qssm-test-build.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for f in BIN.iterdir():
        if f.suffix.lower() in ('.exe', '.dll'):
            z.write(f, f.name)
    z.writestr('qssm-source.patch', patch)
    z.writestr('README.txt', 'QSS-M test build measured on this page. Base commit ' + head + ' plus qssm-source.patch.\\nexe sha256 ' + QSHA + '\\nUse your own Quake assets.\\n')
(NEW / 'gameplay-demos').mkdir(exist_ok=True)""")
for name in ('ctf-fix1.json', 'adtears-fix1.json'):
    pass
rep("""for f in ('ctf-results.json', 'adtears-results.json', 'ctf-installed.json', 'adtears-installed.json', 'demorun.py', 'focusutil.py',""",
    """for f in ('ctf-results.json', 'adtears-results.json', 'ctf-installed.json', 'adtears-installed.json', 'ctf-fix1.json', 'adtears-fix1.json', 'demorun.py', 'focusutil.py',""")

rep("""        ['Rerun driver script', 'data-2026-10-06/refresh.py'], ['Report build script', 'data-2026-10-06/build_report.py']]},""",
    """        ['QSS-M · current build lifecycle', 'data-2026-10-06/lifecycle-qssm-current/results.json'], ['QSS-M · current build FPS', 'data-2026-10-06/fps-qssm-current/fps-results.json'],
        ['QSS-M source patch (this build)', 'data-2026-10-06/qssm-source.patch'], ['QSS-M test build (zip)', 'data-2026-10-06/qssm-test-build.zip'],
        ['Rerun driver script', 'data-2026-10-06/refresh.py'], ['Report build script', 'data-2026-10-06/build_report.py']]},""")

p.write_text(t, encoding='utf-8')
print('patched v4')
