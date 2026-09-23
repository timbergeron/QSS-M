# ad_tears singleton brush dispatch

Investigation dated 2026-09-22, on `68b09fd5` plus the earlier local alias and
external-BSP optimizations. This pass leaves outlines and shadows unchanged.

## Investigation

Fresh diagnostics at the reported north view measured about 0.117 ms per
frame in the opaque brush dispatcher, 0.20 ms in translucent entity drawing,
0.091 ms in view setup, and 0.056 ms in client entity updates. These nested
CPU timings are diagnostic measurements and must not be added together.
Reducing resolution to 1280x720 improved FPS, but the busy views still cost
substantially more than the same-position reference facing away.

Two isolated experiments did not justify production changes:

- Restricting three texture-chain rendering loops to each inline model's
  used texture range avoided scanning 149 entries for many one-texture
  models. In the paired 4K run, medians went from 723.75 to 718.35 FPS at the
  first view and 535.95 to 525.70 at the second. No change was retained.
- Compiling the instanced brush fragment shader with its already-disabled
  grass branch fixed to false gave mixed results: the first view improved
  in the short screening run, the second was essentially unchanged, and the
  reference view declined. This needs stronger evidence before adoption;
  neither shader specialization nor its test controls are in production.

## Change

A sorted model/frame group containing one brush entity cannot be instanced.
Previously it still entered instancing eligibility, potentially built its
cache, and performed culling before the group renderer sent it back to
`R_DrawBrushModel`, which performs its own culling and cache checks.

Dispatch that group directly to `R_DrawBrushModel` once its size is known.
Repeated model/frame groups retain the existing instancing path. Sorting,
visibility rules, lighting, transparency, grass, and special-surface fallback
remain the responsibility of the existing renderer.

## Measured result

Direct dispatcher instrumentation confirmed the work reduction:

| View | Instancing eligibility calls/frame, before / after | Dispatcher ms/frame, before / after |
| --- | ---: | ---: |
| First: `(894 -795 -259) -11 178 0` | 151 / 94 | 0.07713 / 0.07408 |
| Second: `(-954 1958 -180) -9 246 0` | 191 / 131 | 0.09900 / 0.09793 |
| First position facing away, yaw 0 | 144 / 91 | 0.04304 / 0.03994 |

These timings average two two-second samples for each path, collected in
before/after/after/before order. The few-microsecond timing differences are
small; the north samples overlap. The reduced call counts are deterministic.

The whole-frame test does **not establish an FPS improvement**. Medians from
the four retained pairs after warm-up were:

| View | Before FPS | After FPS | p50 ms, before / after | p99 ms, before / after |
| --- | ---: | ---: | ---: | ---: |
| First | 710.85 | 698.65 | 1.370 / 1.410 | 2.085 / 1.985 |
| Second | 528.60 | 526.40 | 1.890 / 1.890 | 2.305 / 2.395 |
| Facing away | 1338.75 | 1322.90 | 0.735 / 0.740 | 1.270 / 1.280 |

Both busy views improved in two pairs and declined in two. The last pair
slowed markedly in both variants. All retained samples are included; these
results are not presented as a gain. The small dispatch simplification is
retained for removing unnecessary checks with unchanged output, but this
pass did not find another substantial FPS optimization.

The FPS test used five paired four-second samples per view, alternating
variant order, with dispatcher instrumentation disabled. Both variants
include all earlier optimizations. Resolution was 3840x2160 desktop
fullscreen (Windows declined exclusive mode), FOV 105, vsync off, uncapped
FPS, outlines 5, shadows 0.125, and brush caching/instancing enabled. Absolute
FPS from this run should not be compared with earlier reports as a new gain.

## Validation

`Misc/stress/test_brush_singleton_dispatch.py` compiles the real comparator
and dispatcher with recording draw/cull stubs. The singleton probe/cull
assertion failed before the change and passes afterward. Cases include
culled and unsupported singletons, repeated models beside singletons,
different animation frames, mixed eligibility, allocation failure, and empty
input. Targeted independent review found no actionable issues.

Paused 3840x2160 comparisons at both reported viewpoints, with `r_alphasort`
both disabled and enabled, are pixel-identical. Repeated baseline controls
are also identical. Release x64 and Win32 builds passed.
The production x64 smoke test reached both exact viewpoints around
`vid_restart`, captured both screenshots, and exited cleanly without fatal
errors. Its binary contains none of this pass's temporary profiling controls.

The isolated fixture uses desktop settings and `id1` assets with the loose
AD map; it retains the missing AD entity-function limitation documented in
the earlier reports. Results describe this fixture, not full AD gameplay.
An initial singleton benchmark stopped reporting frames during warm-up and
was discarded; it is preserved as an incomplete run.

Scripts, diagnostic builds and data are under
`.codex-build/tears-20260922-pass/`. `ranges-prototype/report.json` and
`shader-prototype/report.json` record the exploratory experiments;
`visual/report.json` records the image comparisons. The desktop installation
is untouched.
`singleton-ab-final/report.json` contains all final whole-frame samples;
`dispatch-stages/report.json` contains the direct call/time measurements.
