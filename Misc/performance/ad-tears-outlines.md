# ad_tears deferred outline setup

Investigation dated 2026-09-16, based on `68b09fd5` after pulling `qsrebase`.

The reported views are reproduced with these player positions (the engine
adds the 22-unit camera height):

```text
setpos 894 -795 -259 -11 178 0
setpos -954 1958 -180 -9 246 0
```

## Finding and change

The local desktop configuration uses 3840x2160, FOV 105, `r_outline 5`,
`r_shadows .125`, buffered brush shadows, and alias/brush instancing enabled.
The initial subsystem bisect found entity rendering dominates both views.
Disabling world rendering or grass did not recover the loss. Disabling
outlines reduced median frame time from about 2.31 to 1.51 ms at the first
view and 3.08 to 2.08 ms at the second. This is an upper bound: `r_outline 0`
also allows the existing alias-instancing path, so that difference cannot
all be attributed to outline replay itself.

Deferred outlines previously called the full model renderer three times for
each outlined entity: stencil mask, expanded ring, and stencil scrub. The
change prepares single-surface models once and retains textures, pose/bone
attributes, shader uniforms, and the entity transform for those three draws.
Each draw retains its original GL attribute save/restore and shader resets.
Multi-surface models still complete each phase across every surface before
advancing to the next phase. Triangle statistics retain their original counts.

No quality settings, visibility rules, outline fades, or shadow settings change.

## Results

Medians over the four measured repeats after warm-up:

| Mode / view | Before FPS | After FPS | FPS gain | Before p50 | After p50 | Before p99 | After p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4K window / first | 441.2 | 475.2 | 7.7% | 2.225 ms | 2.095 ms | 2.880 ms | 2.570 ms |
| 4K window / second | 321.7 | 337.8 | 5.0% | 3.120 ms | 2.985 ms | 3.730 ms | 3.325 ms |
| 4K desktop fullscreen / first | 427.3 | 459.5 | 7.5% | 2.315 ms | 2.160 ms | 3.120 ms | 2.820 ms |
| 4K desktop fullscreen / second | 320.6 | 333.4 | 4.0% | 3.100 ms | 2.995 ms | 3.735 ms | 3.695 ms |

The fullscreen improvement was positive in all four retained pairs at both
views. No measured frame exceeded 16.7 ms. The remaining cost is appreciable;
this change does not restore the 1000–1500 FPS observed in simpler views.

## Verification method

- Release x64 build, NVIDIA RTX 4070, copied desktop configuration and local
  assets in an isolated test directory; sound disabled.
- Test-only `test_outline_legacy` switch selects the old dispatch in the same
  executable. It and the stress command channel are generated into ignored
  build sources and are absent from the production change.
- Five interleaved samples per path/view, four seconds each, discard repeat
  zero. Fullscreen run reverses path order on alternating repeats.
- Whole-frame profiler; `host_maxfps 0`, `vid_vsync 0`, `r_speeds 0`,
  `host_speeds 0`. Per-message stress coverage logging is disabled on both
  sides. Exact player and camera positions are checked in the log.
- Paused recorded demos compare the same scene with both paths and with
  `r_alphasort 0` and `1`. All four 3840x2160 comparisons had zero changed
  pixels; repeated legacy captures also had zero changed pixels.
- `Misc/stress/test_outline_replay.py` exercises the production C dispatch
  and actual GLSL pass loop with recording GL stubs. It checks setup counts,
  pass order, stencil/depth/color state, uniform resets, triangle counters,
  mixed single/multi-surface queues, and faded rings. The test passes;
  `--mutation-check` deliberately omits the scrub pass and correctly fails.
  Run with `python Misc/stress/test_outline_replay.py` using `CC`, or from a
  Visual Studio developer shell on Windows.
- Production Release x64 and Win32 builds succeeded. The x64 executable
  subsequently loaded both exact viewpoints, captured screenshots across
  `vid_restart`, and exited cleanly without profiling hooks. This final smoke
  check and the regression test passed on 2026-09-21.

Local build scripts, logs, timings, demo fixtures, and screenshots are under
`.codex-build/tears-20260916/`. The production executable uses no profiling
hooks. The user's existing desktop executable and configuration are untouched.

## Measurement limits

These are incremental savings; substantial entity rendering cost remains.
Absolute FPS depends on scene activity and background load. Compare each
run's interleaved paths, not absolute FPS across runs. The fullscreen attempt
fell back to desktop fullscreen after Windows rejected exclusive mode.
The existing user game session was left running. No non-Windows GPU testing
was performed. The fixture uses the desktop `id1` assets and loose map;
missing AD-specific entity spawn functions appear in its log. It verifies
the renderer under that local fixture, not a complete Arcane Dimensions
gameplay session.

## Follow-up: batch opaque fills with deferred outlines (2026-09-21)

The first change still left a global restriction: `r_outline > 0` disabled
alias-model instancing, even though opaque outlines now render separately.
The second change allows compatible opaque MDL fills to use the existing
instancing path while collecting their outlines with the same size and
proximity checks. The deferred mask/ring/scrub passes remain unchanged.
Player/xray models, powerup pickups, transparent models, unsupported formats,
multi-surface models, and other existing special cases keep their fallbacks.
If the outline queue is full, preparation falls back before invoking a helper
that could otherwise draw immediately.

Batch preparation occurs after individual fills. A transient visible-list
order stamp and a conditional sort restore the original outline replay order,
including when black outlines overlap translucent colored outlines. Duplicate
entities retain their first visible-list position.

Final 4K desktop-fullscreen A/B medians, four retained repeats after warm-up.
The baseline **already includes the first setup-reuse optimization**:

| View | Previous FPS | New FPS | Further gain | Previous p50 | New p50 | Previous p99 | New p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| First | 460.9 | 517.6 | 12.3% | 2.165 ms | 1.915 ms | 2.740 ms | 2.785 ms |
| Second | 342.5 | 390.3 | 14.0% | 2.935 ms | 2.575 ms | 3.390 ms | 3.045 ms |

Every retained pair improved median frame time at both views. Mean FPS
improved in three of four pairs at the first view and four of four at the
second. One first-view sample had a 16.25 ms maximum; the first-view p99
did not improve overall. The earlier exploratory run measured 14.5% and
12.9% FPS gains respectively. These runs show a repeatable throughput gain,
not elimination of all frame-time variability.

The test-only `test_outline_instancing` switch selects the old eligibility
gate or the new path in the same executable; it is absent from production.
The timing methodology, fixture limits, and exclusive-fullscreen fallback
described above still apply. New artifacts are in
`.codex-build/tears-20260921/`, especially `final-instancing-ab/report.json`.

`test_alias_outline_instancing.py` compiles the actual eligibility,
preparation, queue, and ordering helpers with recording stubs. It covers
outline preservation, a full queue, culling, unsupported models, translucent
and xray exclusions, outlines disabled, duplicates, and colored/black order.
`test_outline_replay.py` also checks the actual deferred replay sort and the
first-occurrence ordering stamps. Both pass; the new eligibility and ordering
checks were observed failing before their respective fixes.

The final paused-demo image comparisons changed 35 pixels at the first view
and 68 at the second, with the same results for `r_alphasort 0` and `1`.
Repeated baseline captures had zero differences. This is less than 0.001%
of a 4K image; magnified inspection shows isolated model-edge/texture samples,
consistent with the different matrix arithmetic in the existing instanced
shader. Maximum channel differences were 149/76, so this is **not** a
pixel-identical result: the strict zero-difference probe correctly reports
differences. No quality settings or outline/shadow features were disabled.
Production Release x64 and Win32 builds succeeded after the final change.
The production x64 smoke test reached both exact player/camera positions,
captured images before and after `vid_restart`, and exited with code zero.
Binary checks confirmed that neither A/B switch nor the stress profiler
was included. The desktop installation remains untouched.

Remaining cost includes brush-model submission and shadows. In a separate
instrumented 4K run, opaque brush-model submission consumed approximately
0.33/0.49 ms per frame and shadows 0.21/0.33 ms at the first/second views.
These are nested CPU submission timings, not independent GPU timings.
Reducing resolution to 720p only partly recovered the slowdown, consistent
with substantial work per entity in these dense vistas.
