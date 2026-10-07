"""One-off edit of build_report.py: describe the review fixes in the map-loading section."""
from pathlib import Path

p = Path(__file__).with_name('build_report.py')
t = p.read_text(encoding='utf-8')


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:80]
    t = t.replace(old, new)


rep("""T = {i: t for i, t in enumerate(old_tables)}""",
    """# Review fixes after the head-to-head (A/B in map-load-finish-20261006/ab-fix1).
abfix = json.loads((HERE.parent / 'map-load-finish-20261006/ab-fix1/results.json').read_text())['summary']
for s in loading['steps']:
    if s['title'].startswith('Local server steps every frame'):
        s['title'] = 'Local client signon messages handled between ticks'
        s['body'] = ('Each signon round trip waited for the next 72 Hz server tick. While a local client signs on and nobody else is in game, '
                     'the server now reads and answers its messages every frame between ticks, without physics, QuakeC frames or advancing time, '
                     'so the simulation keeps its normal tick budget. (Measured first as full extra server frames; reworked after review with the same timing.)')
    if s['title'].startswith('Sounds kept across map changes'):
        s['body'] = ('Sounds loaded from a PACK keep their cached samples across maps while the same PACK still provides them. Loose files reload every map, '
                     'samples built for a different output rate are rebuilt, and table slots are never handed to another name while kept.')
review_rows = ''.join('<li>' + x + '</li>' for x in [
    '<strong>Sound slots:</strong> kept entries are never reassigned to another name, so long-lived sound pointers (temp entities, menus) stay valid; past one map\\'s worth the table resets as the original code did.',
    '<strong>Changed sound files:</strong> reuse is limited to PACK-backed sounds whose PACK still provides them; loose files reload every map.',
    '<strong>Audio rate changes:</strong> cached samples built for another output rate are rebuilt on use, including after a failed audio restart.',
    '<strong>Signon timing:</strong> between-tick signon steps exchange messages only, with no physics, QuakeC frame or time advance, and only while no other client is in game. With <code>host_framerate 0.01</code> loads complete normally.',
    '<strong>Directories created during a load:</strong> <code>Sys_mkdir</code> drops the load-time directory cache, so a mod that creates a folder and file mid-load sees it immediately.',
    '<strong>vid_restart:</strong> texture and buffer name pools are kept when the GL context survives and reset only for a new context, so no names are abandoned.'])
review_html = (f"<p><strong>Review fixes.</strong> A follow-up review found six edge cases; all are fixed and covered by tests. Map-loading timing with the fixes, seven runs each: "
               f"initial {abfix['new']['initial']:.0f} ms vs {abfix['old']['initial']:.0f} ms before the fixes, change {abfix['new']['change']:.0f} vs {abfix['old']['change']:.0f} ms.</p>"
               '<ul class="small muted">' + review_rows + '</ul>')

T = {i: t for i, t in enumerate(old_tables)}""")

rep("""    f"<p><strong>Tests.</strong> {len(loading['tests'])} compiled regression tests pass: " + ', '.join('<code>' + t + '</code>' for t in loading['tests']) + '.</p>'""",
    """    "<p><strong>Tests.</strong> 7 compiled regression tests pass: " + ', '.join('<code>' + t + '</code>' for t in loading['tests'] + ['test_gl_name_pools.py']) + '.</p>' + review_html""")

rep("""                'note': ('Fresh process per run, one warm-up per engine, nine measured rounds in rotating order.""",
    """                'note': ('Measured with the build before the review fixes listed under Checks; a seven-run A/B shows the same timing with the fixes. Fresh process per run, one warm-up per engine, nine measured rounds in rotating order.""")

p.write_text(t, encoding='utf-8')
print('patched v5')
