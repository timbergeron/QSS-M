# ad_tears external brush-model batching

Investigation dated 2026-09-21, based on `68b09fd5` plus the two local alias
outline optimizations described in `ad-tears-outlines.md`.

## Finding

Turning from the first reported view toward yaw 0 at the same player position
recovered about 1,323 FPS in the diagnostic fixture. The busy views measured
about 516 and 418 FPS in that run. Client entity updates, view setup, and world
drawing were relatively small; repeated brush-model drawing scaled sharply.

Most opaque brush-cache misses were external BSP pickup and rock models:
`maps/b_shell0.bsp`, `maps/b_nail0.bsp`, `maps/b_bh10.bsp`, `maps/b_bh25.bsp`,
and related models. They already occupy the shared brush vertex buffer and
lightmap atlas, but the cached drawing and instancing gates only accepted
inline models belonging to the current world.

In the diagnostic sample, the first view drew 69 visible external BSP
instances across seven models; the second drew 97 across nine. The older path
revisited surfaces, rebuilt texture chains and triangle-index lists, and
repeated shader setup for these models every frame. Opaque brush submission
took roughly 0.38/0.52 ms at the two views versus 0.08 ms when facing away.
These are CPU submission timings with diagnostic instrumentation enabled.

The 1,024-entry light-style scan initially looked suspicious, but measured
only about 0.0025 ms per frame here because the old cache admitted so few
models. It was not changed.

## Change

Allow supported external BSP models into the existing cached draw and
instanced draw paths. Retain the existing restrictions for transparency,
special effects, dynamic lights, grass, unsupported surfaces, and unavailable
rendering capabilities. World scene-cache skip bits apply only to the world's
inline models, so an external model with the same submodel number is not hidden.

Pending late lightmap/VBO rebuilds use the ordinary draw path first. The cache
then uses the existing vertex-buffer generation and lightmap-count checks to
invalidate indices after buffer rebuilds. Model cleanup already releases
caches for external and inline brush models alike.

Outline and shadow drawing code/settings are unchanged. The comment in the
shadow-cache implementation was updated to describe the new cache eligibility.

## Measured result

One profiling executable toggled only the external-BSP eligibility change;
both variants included the earlier alias optimizations. The test used
3840x2160 desktop fullscreen, FOV 105, uncapped FPS, vsync off, outlines 5,
shadows 0.125, and the copied grass settings. Brush caching and instancing
were enabled. Windows declined the requested exclusive display mode and
the engine used desktop fullscreen.

Five paired four-second samples were collected per view, alternating variant
order. The table gives medians of the last four pairs, excluding the first
warm-up pair. Timings cover whole frames, with stage instrumentation absent.

| Player position / angles | Before FPS | After FPS | Change | Median frame ms, before / after | p99 frame ms, before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| `(894 -795 -259) -11 178 0` | 491.45 | 639.25 | +30.1% | 1.995 / 1.540 | 3.075 / 2.560 |
| `(-954 1958 -180) -9 246 0` | 368.95 | 496.35 | +34.5% | 2.695 / 1.985 | 3.770 / 3.065 |
| First position, facing away at yaw 0 | 1069.30 | 1064.30 | -0.5% | 0.920 / 0.915 | 2.135 / 2.085 |

Every retained pair improved at both reported views. The away view remained
essentially unchanged. The dense views still cost substantially more than
the away view; this removes one measured source of that difference. Absolute
FPS varied between diagnostic runs, so these gains use paired measurements
from this run rather than comparing against earlier reports.

## Validation and artifacts

`Misc/stress/test_external_brush_cache.py` compiles real production cache and
eligibility helpers with recording stubs. It covers external/inline models,
scene-cache skip bits, transparency/effects, late uploads, dynamic-light and
grass fallbacks, disabled caching/instancing, supported surfaces, and cache
invalidation after vertex-buffer or lightmap-layout changes. The external
eligibility check failed before the change and passes afterward.

Release builds passed for x64 and Win32. The production x64 executable loaded
the map, reached both exact viewpoints, captured screenshots before and after
`vid_restart`, and exited cleanly without fatal errors. Its binary contains
none of the temporary A/B or stress-profiler hooks.
Targeted independent code review finished with no actionable findings.

Paused-demo comparisons at 3840x2160 changed two pixels at the first view and
twelve at the second, for both `r_alphasort 0` and `1`. Maximum channel changes
were one and two out of 255 respectively; repeated baseline controls were
pixel-identical. The strict zero-difference image probe therefore reports a
difference; this change is not bit-for-bit identical.
Magnified comparisons show minute color rounding on pickup textures, with
no missing geometry or visible outline/shadow change at these views.

The fixture uses copied desktop settings and `id1` assets with a loose
`ad_tears` map, including the missing AD entity-function limitation documented
in the earlier report. Test-only scripts and artifacts live under
`.codex-build/tears-20260921-brush/`. The desktop installation is untouched.
Final paired measurements are in `brush-ab/report.json`, image measurements
in `visual/report.json`, and production smoke results in `smoke-report.json`.
The production executables are in `release-x64/` and `release-x86/` beneath
the same artifact directory.
