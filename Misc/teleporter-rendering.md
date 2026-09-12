# Teleporter rendering review

`r_telestyle 0` and `1` use the classic surface. Style `2` uses FTE's
refraction shader; style `3` adds its reflection pass. Distortion strengths
remain fixed at `1.0`, and targets remain half the viewport's width and height.
No additional cvars were introduced.

## Changes from the September 11 review

- Save and clear `skyroom_drawn` while rendering subviews, and keep skyroom
  rendering out of the main-view teleporter target path. Subviews draw the
  ordinary sky; they do not recursively render another skyroom.
- Publish the shader only after all uniform lookups succeed. A failed link or
  lookup disables the path until GL reinitialization instead of retrying each
  frame and filling the program table. Precompile active styles at `R_NewMap`,
  after rebuilding a connected video context, and when enabling the effect
  after signon. Disconnected restarts defer compilation until map setup.
  Compiler diagnostics temporarily suppress screen updates to prevent
  reentrant rendering during setup. The existing lazy fallback remains for
  style changes between map setup and signon, or custom client scenes.
- Union visibility from both sides of every collected coplanar face. A probe
  in solid or sky uses the viewer's visibility instead of silently requesting
  the entire map. Each plane/pass remembers its last merged leaf to avoid
  repeated decompression and union work for adjacent faces; the memo resets
  when targets are collected for the next view.
- Replace per-efrag visible-list searches with a separate subview stamp.
- Mark which brush models contain teleporters at load time, avoiding surface
  scans for other models. Maps without usable teleporter normal maps skip
  preparation. Unsupported textures remain exclusively in the cached water
  batches, preventing double drawing when normal-map loading fails. Hidden
  world rendering also skips preparation, and disabled entity rendering skips
  the entire brush-entity collection loop.
- Build brush transforms and model-space eye positions on the CPU. Preserve
  the current stereo eye offset, all four entity scaling pivots, and the
  `gl_zfix` origin nudge used when drawing moving brush models.
- Avoid repeating world dynamic-light marking between subviews. Visibility
  walks and necessary lightmap updates still run per view.
- Cull subview leaves, surfaces and models against the distortion-padded
  sample rectangle and the oblique near plane before updating lightmaps or
  submitting draws. Extract these planes from the actual projection/view
  matrices so reflected views and stereo skew use the same bounds as the GPU.
  Restore the ordinary four-plane frustum with the main view, including aborts.
- Validate framebuffer completeness when allocating each target, rather than
  querying it for every subview every frame. Reuse the depth/stencil attachments
  until resize or context teardown; newly allocated targets are checked again.
- Store generated normal maps in owned heap memory rather than the fixed
  zone. Generate them before diffuse upload, use a name without `*`, and
  protect retained pixels from in-place upload processing. This prevents
  `r_fastturb` and reloads from destroying the distortion map.
- Restore renderer and framebuffer state through an abort handler called by
  `Host_Error` and `Host_EndGame`, including depth/stencil write masks.
- Release reflection targets when switching to style 2 and all targets when
  switching to the classic styles. Unbind newly allocated attachments, bind
  a complete texture to the unused style-2 sampler, and explicitly disable
  attribute array 0 before immediate-mode drawing.
- Honor liquid/entity alpha, consistently classify uppercase `*TELE`
  surfaces (also `*LAVA` and `*SLIME`), preserve player pitch between main views, and damp a reflected
  player copy even when the real player was already visible. Attached
  viewmodels no longer cast shadows into subviews.

## Findings checked against local FTE source

The unusual Fresnel calculation is present in FTE's `altwater.glsl`, including
the exponent-5 behavior when the generated material says `FRESNEL=4`. It is
preserved for visual compatibility. FTE uses model-space vertex normals and
eye positions; transforming just the shader's Normal uniform to world space
would change that convention.

FTE's `GLBE_GenerateBatchTextures` returns false at the recursion limit, and
its caller skips the batch. The proposed classic-water fallback there would
change FTE's behavior. Style 2's unlit diffuse sampling is also present in FTE.
These behaviors and the existing linear normal-map filtering were retained.

Teleporter polygons already contribute to `rs_brushpolys` in surface marking,
brush-model traversal, or the cached polygon count. Only the draw-pass count
is incremented in the shader path; another polygon increment there would
double-count those surfaces.

## Bravado performance investigation

The local FTE `engine/gl/gl_rmain.c:GLR_DrawPortal` adds the portal's clipping
plane to CPU frustum culling. QSS-M previously applied that plane only in the
GPU projection, so it still chained, lit and submitted geometry that was
entirely clipped away. The new CPU frustum also narrows the four side planes
to the existing distortion-padded scissor rectangle. FTE splits portal batches
by plane in `engine/gl/gl_model.c`; Bravado's closely spaced portal faces do
require separate views with that approach too.

Local before/after measurements used Bravado SHA-256
`5f382d591ef5080322c4f8a2da07b22795c66104a7b9a2baccd4e72789d0789d`,
1024x768, Mesa llvmpipe with `LP_NUM_THREADS=2`, `r_scenecache 1`,
`r_telestyle 3`, default effect strengths and half-resolution targets.
Temporary instrumented builds kept `timerefresh` at a fixed camera for 128
frames, using double-precision timing. Each result is the median of three runs
after one warmup; before/after runs were sequential and did not overlap builds.

| Camera (`setpos` arguments) | Extra views | Brush surfaces submitted, before → after | Frame time, before → after |
| --- | ---: | ---: | ---: |
| `-16 1100 24 0 90 0` | 4 | 924 → 694 | 42.35 → 38.27 ms |
| `0 448 24 0 180 0` | 2 | 449 → 287 | 40.31 → 33.65 ms |
| `200 800 100 0 135 0` | 4 | 912 → 325 | 36.06 → 34.31 ms |
| `-16 1100 24 0 270 0` | 2 | 715 → 123 | 39.95 → 34.32 ms |

These are 5–17% reductions in total software-rendered frame time, with timing
noise still present. The deterministic submission counts drop 25–83%.
Style-1 control medians stayed within approximately 4%. These measurements do
not predict Apple GL FPS or establish parity with FTE; no runnable FTE build
was available on this Linux host. The temporary benchmark scripts, instrumented
sources and binaries are in `/tmp/qssm-bravado-perf` for this session.

Fourteen controlled before/after Bravado captures (seven cameras, styles 1
and 3) were pixel-identical below the console notification area. They include
close, oblique and reverse-side views; `cl.time` was fixed for the capture and
animated entities were hidden in both builds. Initial captures with entities
enabled exposed different pickup rotations even in the style-1 controls, so
those were not used for exact comparison. Entity/brush rendering, dynamic
lighting and state restoration also passed the regular renderer test suite.

## Remaining limits

The scene-cache worker must finish pending writes before legacy subviews use
the same lightmap storage. Removing this wait or caching subviews requires
further renderer work. Shadows and lightmap updates still contribute to the
cost of extra views; rendering statistics include their work, and sound
updates remain enabled during expensive rendering.

The 16-plane cap, allocation reuse between views, and classic fallback on
overflow remain. Overflow selection is not sorted by screen area, and an
unmatched face can still send its chain through the classic path. Normal
maps are still generated at map load so runtime style switching has the data
available. There is console/README documentation but no menu entry.

## Validation

- `make -C Quake -j4` with the normal `-Wall` flags.
- `python3 Misc/stress/test_teleport_scissor.py`: 3,520,920 sample taps,
  coplanar bounds/PVS union, per-plane/pass leaf memoization and reset,
  missing-normal cache fallback, solid probes, and close/transformed surfaces.
  Also verifies CPU frustum inclusion against inverse-projected GPU samples
  with reflection, stereo skew, and oblique clipping, and rejects outside boxes.
- `xvfb-run -a python3 Misc/stress/test_teleport_gl.py`: link/uniform failure
  injection, shader warmup and connected/disconnected restart lifecycle,
  8,640 transforms compared with OpenGL (including `gl_zfix` and static/moving
  entities), abort restoration, and target cleanup on style changes. Actual GL
  framebuffer checks cover attachment reuse, validation only on allocation,
  resize/context teardown, and an injected completeness failure.
- `xvfb-run -a python3 Misc/stress/test_teleport_scenecache.py --basedir /path/to/quake`:
  cached/uncached rendering, style changes, moving/baked brush models,
  dynamic lighting, resizing, map changes, uppercase textures, entity alpha,
  and `r_fastturb` across forced video restarts. Also covers a disconnected
  restart followed by loading another map. The test explicitly marks video
  settings dirty because `vid_restart` otherwise does nothing.

Runtime checks used Mesa software rendering. This review did not measure
hardware FPS gains or establish performance parity with FTE.

The QSS-M Xcode scheme's source membership was checked, but `xcodebuild` is
unavailable on this Linux host. Shipping-target compilation remains unverified
until the following is run on macOS:

```sh
xcodebuild -project macOS/QuakeSpasm.xcodeproj -scheme QSS-M -configuration Debug build
```

Apple GL runtime validation is also still pending: view a teleporter with
`r_telestyle 3`, change a video setting and apply `vid_restart`, then change
maps and view another teleporter. Include a restart while disconnected followed
by loading a map. This is needed to check Apple's GLSL 1.10 immediate-mode
path and packed depth/stencil framebuffer support; Mesa results do not verify
either on Apple hardware. Precompilation avoids the explicit first-visible
shader compile but does not establish that this shader suffered the alias
shader's Apple driver issue, or that all first-draw driver work is eliminated.
