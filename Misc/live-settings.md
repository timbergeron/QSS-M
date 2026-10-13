Live graphics, lighting and audio settings
=========================================

These settings now apply automatically while staying on the current map:

- `vid_vsync`: changes the current GL context's swap interval before drawing.
  If SDL rejects or ignores the request, the cvar returns to the actual
  setting. Failed swap-interval queries preserve the last known setting.
  VSync remains available in the menu after a live request is rejected or
  overridden. Mode changes still use the Video menu's Apply/Test controls.
- `gl_picmip`, `gl_max_size`: refresh model and UI textures between simulation
  and drawing. Model skins and sprites also rebuild their padded texture
  coordinates. Other texture users keep their managed texture records.
  Their existing selection rules remain: in particular, `gl_max_size` limits
  ordinary world textures, while external replacements in `textures/` can
  bypass that limit. Ownerless textures skip uploads when their size stays
  unchanged, including textures exempt from picmip; map grayscale and unknown
  packed/external formats retain the conservative refresh path.
  `gl_picmip` accepts integer levels 0–30; `gl_max_size`
  accepts integers from -1048576 to 1048576, with negatives retaining the
  existing grayscale behavior. Invalid nonfinite values become zero.
- `r_fastturb`: refreshes liquid texture selection and uploads, including
  switching between external replacements and the indexed map texture.
- `r_replacemodels`: changing the console cvar now invalidates the model
  cache automatically, matching the graphics menu. Cached aliases reload
  before drawing and entity, static and viewmodel animations reset to the new
  pose layout. Defensive pose checks also cover older cache-flush paths.
  Explicit frame-group blending keeps each group's pose offset and wraps the
  second frame using its own group length.
  An explicit `flush` is no longer needed. Cached sounds stay loaded.
- `gl_loadlitfiles`, `external_lits_dir`: replace lighting samples and rebuild
  lightmaps and brush vertex buffers without respawning the server. Supports
  ordinary RGB `.lit` files, alternate directories, HDR lighting and BSPX
  lighting. Inline models share the refreshed data, HDR state and lighting
  generation. Lighting inputs from the validated BSP stay in map memory, so
  toggles avoid rereading/checksumming the entire map and remain consistent
  with the loaded geometry even if the BSP on disk changes. This retains one
  copy of its raw lighting and any valid BSPX RGB/HDR lighting lumps until the
  map unloads. Changes made while
  disconnected or signing on are applied once the map is ready.
- `r_lightmap_format`: rebuilds lightmaps and brush vertex buffers using the
  requested supported format. Unsupported requests retain the existing
  warning and default-format fallback.
- `snd_surround`: updates the existing SDL stream's channel layout and scratch
  buffer under its stream lock. Turning it off restores stereo without
  reopening the device. Allocation or format failures restore the previous
  preference, including surround enabled on a currently stereo device.
- `snd_mixspeed`: automatically reopens audio at the new mixer rate, once per
  command batch, before music submits more samples. Audio-rate menu arrows
  wait 250 ms after the last click, so cycling rates causes one reopen.
  Console changes and explicit `snd_restart` apply immediately. This can briefly interrupt
  sound; the map and connection stay intact. A failed device open retries the
  previous rate and surround preference. Rates are integers from 8000 to
  192000 Hz; nonfinite values
  return to 44100 Hz. Changes wait while audio is blocked by a video transition
  or focus change. `snd_restart` remains available explicitly.

Texture and lighting rebuilds can cause a short frame hitch. Lightmap rebuilds
reuse existing surface polygons and legacy water subdivisions, so repeated
switches do not append permanent map-memory allocations.
Rebuilds clear stale dynamic-light marks and preserve the render-frame counter.

Validation
----------

`Misc/stress/test_live_settings_callbacks.py` runs production callbacks against
SDL/mixer doubles under address and undefined-behavior sanitizers. It covers
VSync failure rollback, stable frames, deferred/coalesced audio changes,
blocked audio, failed rate changes, stereo/multichannel layout switches,
texture-value bounds and WAV integers with their high bit set. The instrumented
engine run also exposed and fixed a pre-existing signed-shift overflow in WAV
integer parsing.
The review regressions also cover ignored VSync requests and failed queries,
surround allocation failures, stale dynamic-light marks, model-only cache
invalidation, morph/bone pose bounds, lighting generations, retained BSP/BSPX
inputs and malformed `.lit` headers, unchanged ownerless
texture sizes and texture-list traversal when an upload evicts model textures.

`Misc/stress/test_live_settings.py` drives a built engine using offscreen Mesa
and SDL dummy audio. It checks texture sizes and liquid selection, exact
lighting-pixel restoration, RGB/alternate/HDR lighting, lightmap formats,
audio rates, unchanged saved gameplay, stable hunk usage and advancing frame
counters across 60 lighting
rebuilds, and map/save/menu/disconnect/video-restart transitions. `--legacy`
disables shaders, VBOs, NPOT textures and parallel texture preparation;
`--sanitizers` checks an instrumented engine.
Normal runs explicitly enable the scene-cache worker. The lifecycle pass
repeatedly swaps a visible animated soldier to a one-pose MD3 and back.

On 2026-10-13, normal, legacy and ASan/UBSan engine runs passed on Linux with
Mesa llvmpipe and dummy audio, as did the callback tests, lightmap packing,
existing live-texture/lifecycle checks and the live-MSAA engine regression.

Headless tests do not validate visible VSync timing, physical surround
speakers, or Windows/macOS drivers. Those remain hardware checks.
