"""One-off edit of template.html for gameplay demos and investigation sections."""
from pathlib import Path

p = Path(__file__).with_name('template.html')
t = p.read_text(encoding='utf-8')


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:90]
    t = t.replace(old, new)


# Section copy and second heat table
rep("""        <div><div class="eyebrow">Rendering benchmark</div><h2 id="fps-h">Nine maps, one camera sweep</h2></div>
        <p>Native timedemos of the same recorded 360° turn at the first deathmatch spawn of each map. One discarded warm-up, then five fresh-process passes per engine and map, 800 × 600, uncapped, VSync and MSAA off.</p>""",
    """        <div><div class="eyebrow">Rendering benchmark</div><h2 id="fps-h">Nine maps and two real games</h2></div>
        <p>Native timedemos in a fresh process per pass: one discarded warm-up, then five measured passes per engine, 800 × 600, uncapped, VSync and MSAA off. The nine-map suite plays the same recorded 360° turn at each map's first deathmatch spawn. Two gameplay demos add real movement and combat: a 16-player CTF match and Sphere's 2:14 ad_tears speedrun.</p>""")
rep("""      <details class="more" open><summary>All maps at a glance <small>median FPS; select a map to chart it</small></summary><div class="in"><div class="tw"><table class="heat" id="fps-heat"></table></div><p class="note" id="fps-heat-note"></p></div></details>""",
    """      <details class="more" open><summary>All maps at a glance <small>median FPS; select a map to chart it</small></summary><div class="in"><div class="tw"><table class="heat" id="fps-heat"></table></div><p class="note" id="fps-heat-note"></p></div></details>
      <details class="more" open><summary>Gameplay demos <small>median FPS; select a demo to chart it</small></summary><div class="in"><div class="tw"><table class="heat" id="fps-demo-heat"></table></div><p class="note" id="fps-demo-note"></p></div></details>""")

# Picker with groups
rep("""  sel.innerHTML = '<option value="all">All maps · geometric mean</option>' + F.maps.map(m => '<option value="' + m.key + '">' + esc(m.title) + '</option>').join('');""",
    """  const SUITE = F.maps.filter(m => m.group !== 'demo'), DEMOS = F.maps.filter(m => m.group === 'demo');
  sel.innerHTML = '<optgroup label="Nine-map suite"><option value="all">All nine maps · geometric mean</option>' + SUITE.map(m => '<option value="' + m.key + '">' + esc(m.title) + '</option>').join('') + '</optgroup>'
    + '<optgroup label="Gameplay demos">' + DEMOS.map(m => '<option value="' + m.key + '">' + esc(m.title) + '</option>').join('') + '</optgroup>';""")

# Not-applicable rows in per-map chart
rep("""      rows = ORDER.map(k => ({ key: k, label: name(k), sub: ENG[k].version + ' · ' + ENG[k].renderer, s: ms ? toMs(F.stats[fpsMap][k]) : F.stats[fpsMap][k] }));""",
    """      rows = ORDER.map(k => ({ key: k, label: name(k), sub: ENG[k].version + ' · ' + ENG[k].renderer, s: F.stats[fpsMap][k] ? (ms ? toMs(F.stats[fpsMap][k]) : F.stats[fpsMap][k]) : null, na: (m.na || {})[k] }));""")

# Heat tables: suite + demos
rep("""    for (const m of F.maps) h += rowFor(m.title, m.key, k => F.stats[m.key][k].median);
    h += '</tbody><tfoot>' + rowFor('Geometric mean', null, k => F.geomean[k]) + '</tfoot>';
    $('#fps-heat').innerHTML = h;
    $('#fps-heat').addEventListener('click', e => { const b = e.target.closest('[data-map]'); if (!b) return; pickMap(b.dataset.map); $('#fps').scrollIntoView(); });""",
    """    for (const m of SUITE) h += rowFor(m.title, m.key, k => F.stats[m.key][k].median);
    h += '</tbody><tfoot>' + rowFor('Geometric mean', null, k => F.geomean[k]) + '</tfoot>';
    $('#fps-heat').innerHTML = h;
    let d = '<thead><tr><th scope="col">Demo</th>' + ORDER.map(k => '<th scope="col" class="' + (k === 'qssm' ? 'q' : '') + '">' + esc(name(k)) + '</th>').join('') + '</tr></thead><tbody>';
    for (const m of DEMOS) d += rowFor(m.title, m.key, k => F.stats[m.key][k] ? F.stats[m.key][k].median : null);
    $('#fps-demo-heat').innerHTML = d + '</tbody>';
    $('#fps-demo-note').innerHTML = DEMOS.map(m => '<strong>' + esc(m.title) + ':</strong> ' + esc(m.note)).join('. ') + '. Dashes mark engines that can\\'t play the demo.';
    for (const id of ['#fps-heat', '#fps-demo-heat']) $(id).addEventListener('click', e => { const b = e.target.closest('[data-map]'); if (!b) return; pickMap(b.dataset.map); $('#fps').scrollIntoView(); });""")

# rowFor tolerant of null values
rep("""      const vals = ORDER.map(get), top = Math.max(...vals);""",
    """      const vals = ORDER.map(get), top = Math.max(...vals.filter(v => v != null));""")
rep("""      ORDER.forEach((k, i) => { const v = vals[i]; r += '<td class="'""",
    """      ORDER.forEach((k, i) => { const v = vals[i]; if (v == null) { r += '<td class="' + (k === 'qssm' ? 'q' : '') + '" title="Not applicable">–</td>'; return; } r += '<td class="'""")

# Standings rows for gameplay demos
rep("""    rowsDef.push({ key: 'fps', label: 'FPS · nine-map mean', sub: 'geometric mean · higher is faster', higher: true, stat: k => D.fps.geomean[k], unranked: [], digits: 0 });""",
    """    rowsDef.push({ key: 'fps', label: 'FPS · nine-map mean', sub: 'geometric mean · higher is faster', higher: true, stat: k => D.fps.geomean[k], unranked: [], digits: 0 });
    for (const m of D.fps.maps.filter(x => x.group === 'demo'))
      rowsDef.push({ key: 'fps:' + m.key, label: 'FPS · ' + m.title, sub: 'median FPS · higher is faster', higher: true, stat: k => D.fps.stats[m.key][k] ? D.fps.stats[m.key][k].median : null, unranked: [], digits: 0 });""")
rep("""      if (b.dataset.go === 'fps') { pickMap('all'); location.hash = 'fps'; return; }""",
    """      if (b.dataset.go === 'fps') { pickMap('all'); location.hash = 'fps'; return; }
      if (b.dataset.go.startsWith('fps:')) { pickMap(b.dataset.go.slice(4)); $('#fps').scrollIntoView(); return; }""")

# Investigation sections
rep("""  const I = D.investigation;
  $('#inv-date').textContent = I.date;
  $('#inv-findings').innerHTML = I.findings.map(f => '<div><h4>' + esc(f.h) + '</h4><p>' + f.p + '</p></div>').join('');""",
    """  const I = D.investigation;
  $('#inv-date').textContent = I.sections[0].date;""")
rep("""  $('#inv-panels').innerHTML = I.panels.map(p => {""",
    """  const panelHtml = p => {""")
rep("""    return '<details class="more"><summary>' + esc(p.title) + '<small>' + esc(p.sub || '') + '</small></summary><div class="in">' + body + '</div></details>';
  }).join('');""",
    """    return '<details class="more"><summary>' + esc(p.title) + '<small>' + esc(p.sub || '') + '</small></summary><div class="in">' + body + '</div></details>';
  };
  $('#inv-findings').innerHTML = I.sections[0].findings.map(f => '<div><h4>' + esc(f.h) + '</h4><p>' + f.p + '</p></div>').join('');
  $('#inv-panels').innerHTML = I.sections[0].panels.map(panelHtml).join('') + I.sections.slice(1).map(s =>
    '<h3 class="inv-sub">' + esc(s.title) + ' <span class="flag old">' + esc(s.date) + '</span></h3><div class="findings">' + s.findings.map(f => '<div><h4>' + esc(f.h) + '</h4><p>' + f.p + '</p></div>').join('') + '</div>' + s.panels.map(panelHtml).join('')).join('');""")
rep("""        <div><div class="eyebrow">QSS-M FPS investigation · <span id="inv-date"></span></div><h2 id="inv-h">Where QSS-M's frame time goes</h2></div>
        <p>Diagnostic runs that look for the source of QSS-M's FPS gap. These use instrumented or private builds, so their numbers are kept out of the rankings above.</p>""",
    """        <div><div class="eyebrow">QSS-M FPS investigation · <span id="inv-date"></span></div><h2 id="inv-h">Why QSS-M's timedemo FPS was low</h2></div>
        <p>Diagnostic runs that trace QSS-M's FPS gap. They use instrumented or private builds, so their numbers stay out of the rankings above. The October 5 work is kept below for reference.</p>""")
rep(""".findings > div { border-top: 3px solid var(--bronze);""",
    """.inv-sub { font-size: 24px; margin: 30px 0 12px; }
.findings > div { border-top: 3px solid var(--bronze);""")

p.write_text(t, encoding='utf-8')
print('template patched')
