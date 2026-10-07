"""One-off edit of build_report.py: Oct 6 investigation section + gameplay demo data wiring."""
from pathlib import Path

p = Path(__file__).with_name('build_report.py')
t = p.read_text(encoding='utf-8')


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:80]
    t = t.replace(old, new)


rep("""investigation = {
    'date': 'October 5, 2026 · driver 591.86',""", """inv6 = json.loads((EXP / 'investigation-20261006.json').read_text())
dp = inv6['demo_precache']
precache_rows = [['Map', 'Before (FPS)', 'After (FPS)', 'Change']]
for m in dp['before']:
    b, a = dp['before'][m]['median'], dp['after'][m]['median']
    precache_rows.append([MAP_TITLES.get(m, m), f"{b:,.0f}", f"{a:,.0f}", f"{(a / b - 1) * 100:+.0f}%"])
pb, pa = inv6['passes_before'], inv6['passes_after']
pass_rows = [['Pass in one process', 'Before: FPS', 'slowest frame', 'After: FPS', 'slowest frame']]
for i in range(len(pa['fps'])):
    pass_rows.append([str(i + 1), f"{pb['fps'][i]:,.0f}", f"{pb['probes'][i]['max_ms']:.0f} ms", f"{pa['fps'][i]:,.0f}", f"{pa['probes'][i]['max_ms']:.0f} ms"])
st = inv6['settle']
settle_rows = [['Before the first timedemo', 'Pass 1', 'Pass 2', 'Pass 3'],
               ['Nothing (as in the benchmark)'] + [f"{v:,.0f}" for v in st['none']],
               ['3,000 idle frames, uncapped'] + [f"{v:,.0f}" for v in st['idle_3000_frames']],
               ['~2 seconds idle at 144 FPS'] + [f"{v:,.0f}" for v in st['idle_2s_capped']] + [''],
               ['Ironwail, nothing'] + [f"{v:,.0f}" for v in st['ironwail_same_process']]]
hd = inv6['hud']
def hrow(label, k):
    s = hd[k]['stages']
    return [label, f"{hd[k]['fps'][0]:,.0f}", f"{hd[k]['fps'][-1]:,.0f}", f"{s['swap']:.0f}", f"{s['screen'] - s['swap']:.0f}", f"{s['read']:.0f}"]
budget_rows = [['Aerowalk, QSS-M', 'Pass 1 FPS', 'Warm FPS', 'Swap µs', 'Other drawing µs', 'Demo read µs'],
               hrow('Normal', 'default'), hrow('HUD hidden', 'no_hud'), hrow('No world, HUD on', 'no_world_hud'), hrow('No world, no HUD', 'no_world_no_hud')]
sw = inv6['swaptest']
swap_rows = [['Minimal window, clear and swap', 'Swap µs per frame', 'Max FPS'],
             ['SDL2', f"{sum(sw['sdl2_us']) / 2:.0f}", f"{1e6 / (sum(sw['sdl2_us']) / 2):,.0f}"],
             ['SDL3 (QSS-M)', f"{sum(sw['sdl3_us']) / 2:.0f}", f"{1e6 / (sum(sw['sdl3_us']) / 2):,.0f}"],
             ['SDL3 + 70 immediate-mode quads', f"{sw['sdl3_70quads_us']:.0f}", f"{1e6 / sw['sdl3_70quads_us']:,.0f}"]]
gl = inv6['glcalls_per_frame']
gl_rows = [['Draw function', 'glBegin blocks per frame']] + [[k, str(v)] for k, v in gl['glBegin_sites'].items()]
oct6 = {
    'date': 'October 6, 2026 · driver 610.88',
    'title': 'Why QSS-M\\'s timedemo FPS was low',
    'findings': [
        {'h': 'Map loading was inside the timer (fixed)', 'p': 'QSS-M loaded a demo\\'s models, sounds and lightmaps during the first timed frames, about 100 ms on Aerowalk. Other engines load while parsing the first message, before timing starts. Loading demo precaches up front raised fresh-process FPS by 26–33%.'},
        {'h': 'The first ~2 seconds run in single-threaded driver mode', 'p': 'NVIDIA switches its OpenGL driver to threaded mode about two seconds after an engine starts rendering, with one 60–80 ms stall. A fresh-process timedemo runs entirely before that switch. Every engine pays it, but QSS-M\\'s per-frame driver work makes it pay more.'},
        {'h': 'The HUD is the next target', 'p': 'Every frame pays a fixed ~190 µs windowed present on this PC, the same with SDL2 or SDL3. Beyond that, QSS-M\\'s immediate-mode HUD costs about 26 µs per frame; hiding it lifts warm FPS from 3,097 to 3,391, past Ironwail. Batching 2D drawing is the planned fix.'},
    ],
    'panels': [
        {'title': 'Demo precache moved out of the timed frames', 'sub': 'shipped · fresh-process FPS, five passes per map', 'intro': 'Same source and harness, before and after loading demo precaches inside the serverinfo frame (cl_parse.c). Demos can\\'t download files, so nothing is lost by loading early.', 'tables': [{'caption': 'Median FPS, one warm-up process then five fresh processes per map', 'rows': precache_rows}, {'caption': 'Aerowalk, passes in one process: the before build has a 90–100 ms load frame in every pass', 'rows': pass_rows}],
         'links': [['Investigation data', 'data-2026-10-06/gameplay-demos/investigation-20261006.json'], ['Probe patch', 'data-2026-10-06/gameplay-demos/probe-tdframe-stages-glcount.patch']]},
        {'title': 'Driver warm-up in fresh processes', 'sub': 'why pass 1 is slower than pass 3', 'intro': 'The one-time stall lands wherever the driver switches modes. Idling about two seconds first, even at only 144 FPS, moves it before the first pass; frame count alone does not. Ironwail warms up across passes the same way.', 'tables': [{'caption': 'Aerowalk FPS per pass, same process', 'rows': settle_rows}]},
        {'title': 'Frame budget', 'sub': 'per-frame CPU time, µs, warm pass', 'intro': 'Timers around each stage of QSS-M\\'s frame. The swap includes the driver processing the frame\\'s GL commands. A bare clear-and-swap test shows ~190 µs is the windowed present floor on this PC for any engine.', 'tables': [{'caption': 'QSS-M stage timings', 'rows': budget_rows}, {'caption': 'Present floor test', 'rows': swap_rows}, {'caption': 'Immediate-mode HUD draws per frame', 'rows': gl_rows}],
         'after': 'Renaming the executable (quakespasm.exe, bench.exe, ironwail.exe) changed nothing, so no NVIDIA application profile is involved. Sound mixing costs about 3 µs per frame.',
         'links': [['Swap test source', 'data-2026-10-06/gameplay-demos/swaptest.c'], ['Profiler script', 'data-2026-10-06/gameplay-demos/profile_td.py']]},
    ],
}
oct5 = {
    'date': 'October 5, 2026 · driver 591.86',
    'title': 'Earlier investigation',""")

rep("""         'links': [['Before/after JSON', 'fps/optimization/hud-cache/gpu-results.json'], ['Source patch', 'fps/optimization/hud-cache/source.patch'], ['Regression test', 'fps/optimization/hud-cache/test_observer_hud_cache.py']]},
    ],
}""", """         'links': [['Before/after JSON', 'fps/optimization/hud-cache/gpu-results.json'], ['Source patch', 'fps/optimization/hud-cache/source.patch'], ['Regression test', 'fps/optimization/hud-cache/test_observer_hud_cache.py']]},
    ],
}
investigation = {'sections': [oct6, oct5]}""")

rep("""    'fps': {'maps': [{'key': mp, 'title': MAP_TITLES.get(mp, mp), 'note': f"{frames[mp]:,} frames"} for mp in maps],""",
    """    'fps': {'maps': [{'key': mp, 'title': MAP_TITLES.get(mp, mp), 'note': f"{frames[mp]:,} frames", 'group': 'suite'} for mp in maps] + workloads,""")

rep("""            'note': 'Timedemo measures playback throughput in a fresh process; it is not a prediction of live-match FPS. See the FPS investigation below for warm-process results.'""",
    """            'note': 'Timedemo measures playback throughput in a fresh process; it is not a prediction of live-match FPS. Gameplay demos use the same method; QSS-M runs them with scr_autoid 0, and Ironwail and vkQuake get no unfocused sleep. See the FPS investigation for warm-process results.'""")

p.write_text(t, encoding='utf-8')
print('patched v3')
