# ad_tears shadow and outline submission

Investigation dated 2026-09-22, on `7bdc31ec` (after the alias outline
instancing, external BSP cache and singleton dispatch commits).

## Finding

A non-admin stack sampler on the main thread showed `RecursiveLightPointSample`
as the largest engine function (~14% of samples at the north view). Caching it
removed 99.5% of light traces but changed FPS by 0%: the main thread's CPU
work overlaps the NVIDIA driver's worker thread, which was saturated. At these
views the frame is bound by **GL command submission**, so the lever is fewer
draw calls and state changes, not engine CPU work.

An in-process feature bisect (every variant restating every toggled cvar) at
the north view, 4K, before this work:

| Disabled | Frame time saved |
| --- | ---: |
| outlines | 0.53 ms |
| shadows | 0.50 ms |
| both | 0.97 ms |
| all entities | 1.20 ms |
| world, particles, grass, viewmodel, HUD | ~0 |

Per frame there were 35 deferred outlines (3 draws each) and 79 alias shadows
(1 draw each, full per-entity program/attribute/uniform setup) plus brush
shadows with a dozen fixed-function state toggles around each draw. That is
roughly 2-3 µs of driver time per draw call.

## Changes

1. **Ordered shadow queue** (`r_alias.c`, `r_brush.c`, `gl_rmain.c`). The
   stencil lets the first shadow drawn over a pixel win, so shadow draw order
   decides overlaps and must be preserved wherever two shadows can meet. Every
   alias and brush shadow is queued in visible-list order with a conservative
   view cone around its footprint (the model's bounds, turned by yaw where
   possible, pushed through the same skew/squash chain as the draw; pitched or
   rolled models fall back to a rotation-safe sphere, scaled brush models to
   "overlaps everything"). Disjoint cones can never share a pixel. The queue
   is split into waves -- each shadow's wave is 1 + the highest wave of any
   earlier shadow whose cone meets its own -- so every possibly overlapping
   pair is drawn in queue order, and inside a wave nothing overlaps. Each wave
   then draws brush shadows under one fixed-function state and identical plain
   MDL shadows (model, poses, shadow texture) as one instanced draw, with the
   shadow projection folded into the instance matrix and the alpha into the
   instance light colour. A full queue is flushed before it takes more.
   (A first version sorted shadows freely; it was only identical in the tested
   scenes, since overlapping shadows of different opacity could change or
   flicker as animation regrouped batches.)
2. **Per-model stencil values for the outline replay** (`r_alias.c`). With at
   least 5 stencil bits, each deferred outline masks with its own value in the
   bits below the viewmodel/laser bit and its ring tests "not my value", so the
   per-model scrub draw is gone. One clear before and after the replay leaves
   the stencil exactly as the scrubbed replay did; running out of values clears
   and restarts. Fewer bits keep the legacy mask/ring/scrub replay.
3. **Light cache eligibility** (`gl_rlight.c`, `r_brush.c`). The per-entity
   light-point cache no longer requires the entity to sit at its baseline (it
   is keyed on world, model and exact origin already), caches misses instead of
   tracing twice, and brush shadows use it. A trace that never crosses a plane
   leaves `lightspot` as the previous caller set it; such misses are flagged
   and never cached, so they keep the old per-call behaviour. CPU saving only;
   no FPS change at these driver-bound views. Commit it separately.

## Measured result

In-process A/B (one client, a test-only cvar toggling each change, five
alternating 3 s pairs per view, 3840x2160, FOV 105, outlines 5, shadows 0.125):

| Change | North | South | Away |
| --- | ---: | ---: | ---: |
| Ordered shadow queue (both shadow kinds) | +16.5% | +9.6% | +0.7% |
| Outline per-model stencil values | +11.2% | +9.6% | +5.2% |
| Light cache eligibility | -1.1% (noise) | +0.1% | -0.1% |

Every in-process A/B screenshot pair was pixel-identical at all three views.

Whole change, separate launches of the `7bdc31ec` build and the new build
alternating (3 launches each, 2 samples per view per launch, throttled launches
discarded):

| View | Before | After | Gain |
| --- | ---: | ---: | ---: |
| North `(-954 1958 -180) -9 246 0` | 580 | 730 | +26% |
| South `(894 -795 -259) -11 178 0` | 805 | 943 | +17% |
| Away (south position, yaw 0) | 1831 | 1931 | +5% |

At the north view the queue splits ~169 shadows into ~10 waves with ~12
instanced runs per frame. Looser footprints (a sphere holding the model in any
orientation) produced 33 waves and most of the gain back.

After these changes outlines (~0.38 ms at north, from 0.53) are the largest
remaining entity cost.

## Validation

- `Misc/stress/test_shadow_queue.py` (new) compiles the production footprint,
  wave, flush and make-room code: 4000 random models/poses/heights/eyes check
  that every model point pushed through the engine's own transform chain lies
  in its cone (alias yaw and pitched, brush yaw and rotated, scaled brush);
  random queues check that overlapping pairs always draw in queue order, waves
  draw in order, batched shadows are never also drawn singly, the brush state
  is closed before alias draws, a failed upload falls back, and a full queue
  flushes first. Deliberately shrinking the footprint, merging waves, moving
  the footprint plane or ignoring waves for brush shadows all fail it.
- `Misc/stress/test_outline_replay.py` covers the legacy replay, per-model
  values (no scrub, viewmodel bit untouched, clears before/after), wrap-around
  with the clear timed between the last use of a value and its reuse, faded
  rings and state restoration. The ring stub now runs the production stencil
  ref/mask derivation and the test asserts the real ring feeds it to
  `glStencilFunc(GL_NOTEQUAL, ...)`. Its `--mutation-check` still fails as
  intended. The alias instancing, external brush cache and singleton dispatch
  tests pass unchanged.
- Release x64 and Win32 build without warnings in the changed files; the
  release binary has no test hooks.
- Cross-build screenshots against `7bdc31ec` at north and south were identical
  for default settings, `r_alphasort 0`, `gl_alias_instancing 0`,
  `r_outline 0`, `r_shadows 0`, `r_shadows 1`, `r_shadows_buffered 0`, and
  after `vid_restart`; at the away view all but the first capture after the
  demo loads were identical, and a same-build control run shows the same
  first-capture difference, so cross-launch images are not bit-stable there.
  Every in-process A/B pair (same client, change toggled) was identical at all
  three views.
- The fixture is the same loose `ad_tears` map on `id1` assets as the earlier
  reports; full AD gameplay remains to be checked.

Scripts and data: `.codex-build/claude-prof/` (`toggle_ab.py`, `featbisect.py`,
`sampler.py`, `ab.py`, `compare.py`; results in `*-ab/`, `bisect*/`,
`ab-final/`, `compare*/`).

## Next candidate

Outlines are now the largest entity cost. Most outlined models at the north
view are distant, non-overlapping flying creatures. Models whose screen
rectangles (expanded by the outline width) overlap no other outlined model
could have their mask and ring passes instanced per model/pose group with an
exact result; it needs a per-instance outline alpha in the instanced shader.
