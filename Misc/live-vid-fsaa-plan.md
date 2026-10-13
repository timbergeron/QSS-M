Make `vid_fsaa` live
====================

Goal: changing `vid_fsaa` from the console or graphics menu takes effect on
the next rendered frame, while staying connected to the same map. No map
reload, `vid_restart`, or engine restart is needed on supported hardware.
Implemented in the working tree. The design and validation scope
below describe the change; platform checks still outstanding are listed at the end.

Original behavior before this change:

- `Quake/gl_vidsdl.c`: `VID_Init` reads `vid_fsaa` into the startup-only
  `fsaa` variable. `VID_SetMode` requests SDL window multisampling and records
  `fsaa_obtained`. `VID_FSAA_f` only prints an engine-restart warning.
  `VID_Restart` keeps the existing window/context, so routing the callback
  through that command would not implement live sample-count changes.
- `Quake/gl_rmain.c`: `GLSLGamma_BeginFrame` already supports a scene FBO,
  but its attachments are single-sampled and it runs only when gamma or
  software emulation needs a post-pass.
- `Quake/gl_screen.c`: FXAA has its own single-sample FBO and currently zeros
  `vid_fsaa` during lazy initialization. Crosshair drawing also unconditionally
  disables `GL_MULTISAMPLE` after use.
- `Quake/menu.c`: the graphics menu already sets `vid_fsaa` directly, cycling
  through 0, 2, 4, 6, 8, and 16.

Implemented design: render to an engine-owned multisample framebuffer, then
resolve its color to a single-sample texture before presentation. This lets
the renderer replace its attachments without replacing the SDL window or GL
context. Multisample renderbuffers and explicit resolves are supported by
[ARB_framebuffer_object](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_framebuffer_object.txt)
or the corresponding EXT extensions.

1. Add capability detection and separate requested/applied state.

   In `gl_vidsdl.c` and `glquake.h`, load `glRenderbufferStorageMultisample`,
   `glBlitFramebuffer`, and renderbuffer sample queries, with EXT variants
   where needed. Require GL 3.0/ARB FBO support or the complete EXT FBO,
   multisample, blit, and packed-depth/stencil feature set. Basic FBO support
   alone is insufficient. Reset capability flags and pointers on GL reinit.

   Keep the requested `vid_fsaa` value separate from the actual applied sample
   count. Validate values before integer conversion, query `GL_MAX_SAMPLES`,
   and verify actual color/depth sample counts after allocation. A driver may
   round a request such as 6x up to a supported count. Report the applied count
   and any fallback in diagnostics and the graphics menu.

2. Make scene-target ownership independent of gamma.

   In `gl_rmain.c`, generalize the existing gamma scene-target lifecycle into
   a small scene-target API. Allocate a multisample color renderbuffer and
   matching depth/stencil renderbuffer, plus a single-sample color texture/FBO
   for resolves. Use matching sized color formats and dimensions for resolves;
   validate framebuffer completeness only when allocating or rebuilding.

   With MSAA enabled, bind the multisample target before any frame drawing,
   including full CSQC views. At frame end, resolve color at equal dimensions
   using `GL_NEAREST`. Feed the resolved texture directly to the existing
   gamma/soft-emulation pass, or present it with a plain textured quad when
   that pass is unnecessary, including hardware-gamma mode. Keep the existing
   direct rendering path when both MSAA and the gamma post-pass are off.

   Preserve stencil behavior and make stencil availability describe the
   active render target. Replace hard-coded gamma-target restoration in FXAA
   and menu previews with the scene-target API. Teleporter passes must restore
   the actual target they saved. Preserve read/draw bindings, viewport, scissor,
   and relevant GL state around resolves; recover bindings after an aborted frame.

3. Handle framebuffer readers before enabling the new path.

   `R_RenderSceneBlur` and `R_WarpScaleView` use `glCopyTexSubImage2D`.
   Reading/copying directly from a user-created multisample FBO is invalid under
   the [EXT multisample specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_framebuffer_multisample.txt).
   Add a helper that resolves the current scene color, binds that single-sample
   FBO for reads, performs the existing copy, then restores read/draw bindings.
   Continue drawing into the original scene target so later views retain their
   depth/stencil contents. Resolve again after subsequent drawing when needed;
   an earlier resolve is not a valid snapshot of later writes.

   Audit screenshots and every other framebuffer read/copy call. Screenshots
   taken between frames should read the final presented image; in-frame capture
   must use a resolved source. Test sub-view rectangles, render scaling, and
   underwater warp separately from the final full-size resolve.

4. Apply setting changes at a safe frame boundary.

   Replace `VID_FSAA_f`'s warning with validation and a pending-change flag.
   At frame start, after `GL_BeginRendering` supplies drawable dimensions,
   apply the pending sample count and allocate targets before drawing begins.
   Snapshot the active AA mode for the entire frame: a CSQC command changing
   a cvar during drawing must affect the following frame, not switch targets
   halfway through the current frame.

   Rebuild only AA/scene attachments when samples or drawable size change.
   Reuse targets otherwise; do not reload map data, textures, meshes, or shaders.
   Restore `GL_MULTISAMPLE` state after crosshair drawing and explicitly establish
   it when binding the MSAA scene target. Tear down targets on video restart,
   actual context loss, and shutdown.

5. Integrate startup, FXAA, and failure handling.

   Create the SDL window without multisampling and apply startup `vid_fsaa` or
   `-fsaa` through the same renderer path. Synchronize the command-line request
   with the cvar. Update `fsaa_obtained`, `videosetg`, and initialization messages
   so they distinguish window samples from renderer samples. On macOS, remove
   the hardware-gamma preference triggered solely by window MSAA; preserve
   explicit gamma overrides and existing gamma fallback behavior.

   Keep MSAA and FXAA mutually exclusive. Policy: the last enabled
   setting wins during runtime; when both are present at startup, FXAA takes
   precedence, matching current initialization behavior. Resolve this centrally
   before frame drawing, with guarded cvar updates, instead of inside `FXAA_Init`.
   Defer FXAA resource destruction to the same frame boundary.

   Build replacement targets completely before publishing them. If allocation
   fails, try lower supported counts and then render safely without MSAA; clean
   up partial allocations and report the effective count once. Avoid retrying
   the same failing request every frame. Retry on a new request, drawable-size
   change, or GL reinit. On hardware without the required extensions, retain
   existing non-MSAA rendering and explain that live MSAA is unavailable.
   This implementation drops startup-only window MSAA on that legacy hardware;
   retaining that separate legacy mode would add startup/context complexity.

6. Validate behavior and performance.

   Add a focused real-GL regression harness following `Misc/stress/test_teleport_gl.py`,
   with injected capability/allocation failures where physical hardware cannot
   exercise them. Verify actual samples, valid resolves, pixel output, resource
   cleanup, and absence of GL errors. Use engine-level scenarios to cover:

   | Scenario | Required result |
   | --- | --- |
   | Cycle 0 → 2 → 4 → 8 → 16 → 0 on one loaded map | Next-frame application; connection and map remain intact |
   | Console, graphics menu, config, and `-fsaa` | Consistent requested/applied behavior |
   | Gamma/contrast defaults and changes, soft emulation, macOS hardware gamma | Correct image and brightness throughout switches |
   | FXAA/MSAA switches, including both startup values | Deterministic selection; balanced begin/end targets |
   | Water warp, motion blur, `r_scale`, small viewports, stereo | Valid reads/copies and correct composition |
   | Full CSQC, HUD, crosshairs, menu previews, teleporters | Correct target restoration and MSAA state |
   | Resize, HiDPI, fullscreen, minimize/restore, `vid_restart`, quit | Correct target size, cleanup, and recovery |
   | Unsupported extensions/counts, allocation failure, malformed values | Safe rendering, accurate reporting, no retry/log loop |
   | Repeated toggles and unchanged frames | No resource growth or per-frame target allocation |

   Compare representative timedemos at 0x and 4x against the baseline. The 0x
   path should retain its existing cost; measure the added resolve and allocation
   hitch with MSAA enabled. Validate Linux, Windows, and macOS, including an EXT
   compatibility context where available. Do not claim untested platforms pass.

Suggested delivery order: capability/state support, scene-target and read/copy
integration, live callbacks/startup/menu behavior, then regression and platform
validation. Completion means sample changes visibly affect subsequent frames on
the same map, all reader/effect paths remain valid, and disabled MSAA preserves
the existing rendering fast path.

Implementation and validation
-----------------------------

`vid_fsaa 4` enables MSAA on the next frame; `vid_fsaa 0` disables it.
`vid_describecurrentmode` reports requested and applied sample counts. The
graphics menu offers power-of-two requests up to `GL_MAX_SAMPLES` (at most 16x)
and displays `requested (applied)` when the driver rounds the request. Console
values outside that menu sequence step to the nearest choice in the selected
direction. `GL_MAX_SAMPLES` is only a ceiling; it does not enumerate exact
supported counts. Unsupported requests fall back to
lower counts or off without repeating allocations or warnings every frame.
An explicit `-fsaa` overrides startup config values. Otherwise, FXAA takes
precedence when a startup config enables both; runtime selection uses the last
enabled AA setting. Neither cvar callback changes GL resources during drawing.
An AA switch prints a one-line note naming the setting it disables.
Sample/failure diagnostics are emitted on state changes, and repeated allocation
failures during resize do not repeat warnings until recovery. Color/depth buffers
of at least 512 MiB produce a warning once on crossing that threshold; the
estimate uses the applied count and excludes resolve storage and driver overhead.

The scene target includes packed depth/stencil. Its color resolves into the
existing gamma texture, and motion blur, underwater warp/render scaling, and
in-frame screenshots resolve before reading it. Crosshairs preserve MSAA state.
Scene attachments are released on video restart and shutdown. With MSAA and
gamma/soft emulation off, drawing retains the direct window path.

Automated checks passed on Linux with SDL's offscreen driver and Mesa llvmpipe:

- `Misc/stress/test_live_fsaa.py`: production resource and presentation code in
  real GL 4.5 compatibility and GL 2.1 contexts. Checks visible edge coverage,
  next-frame setting changes, actual sample queries, fresh resolves, read/draw
  and scissor restoration, resize/aborted-frame recovery, gamma and plain
  presentation, injected allocation/capability failures, input validation,
  resource deletion, and 100 stable frames without target allocations/checks.
  Resolve texture/FBO failures also check safe window rendering, warning
  reentrancy protection, failure latching, and recovery on a changed request.
  A resize with MSAA off followed by a mid-frame gamma enable checks that
  color and depth/stencil attachments are rebuilt to matching dimensions.
  Independent read/draw bindings, reads from another target during a scene
  frame, and valid framebuffer names with their high bit set are also covered.
  Unsupported-softemu warnings simulate the console's nested redraw and verify
  that the resolved frame keeps its original pixels, with GLSL gamma unavailable
  and with insufficient texture units.
  Menu cycling checks cover driver limits, unsupported MSAA, and custom console
  values in both directions. Resize loops verify that rounded-sample messages
  and persistent texture/FBO/MSAA failure warnings stay quiet. Both AA callbacks
  are checked for their explanatory switch notices.
- `Misc/stress/test_live_fsaa_loader.py`: the production capability loader with
  simulated core/ARB and EXT-only contexts. Checks API-family selection even
  when unsupported function names return non-null addresses, missing extensions
  and entry points, NPOT requirements, and pointer resets on context changes.
- `Misc/stress/test_live_fsaa_engine.py`: the built engine on `start`, staying
  connected through 0/2/4/6/8/16/off transitions, FXAA presets and MSAA switches,
  gamma/contrast/soft emulation, motion blur, underwater warp, render scaling,
  smaller viewports, stereo, crosshairs, menu display, connected video restart,
  screenshots, repeated toggles, and clean shutdown. Also exercises `-fsaa`
  over conflicting config values, FXAA-first config ordering when both AA
  modes are enabled, and the presentation path with GLSL gamma disabled.
  Compatibility runs with VBOs, GLSL, and multitexture disabled separately,
  as well as GLSL and multitexture disabled together, also pass. A run with
  `-notexturenpot --expect-no-msaa` verifies that all MSAA requests fall back to
  zero applied samples while other rendering and gamma paths remain usable.
  With `--fteqcc`, compiles a full CSQC fixture and verifies opaque/translucent
  UI pixels across mid-frame MSAA/FXAA changes, UI-only frames, and a callback
  that leaves a scissor clip active, in both GL 4.5 and GL 2.1 contexts.
  The native setup menu's player-preview pixels are checked across MSAA,
  FXAA, and gamma changes, including an empty-preview control with entities
  disabled, and connected video restart with the preview open in both AA modes.
  Captures wait for a new, complete image rather than assuming a
  fixed screenshot-worker delay. Mode checks also wait for a rendered frame
  to apply callback changes and report an engine exit with its artifact path.
  No renderer GL errors. In GL 2.1, SDL's EGL context probe generates a known
  `GL_MAJOR_VERSION` query error before renderer initialization; the harness
  reports and excludes exactly that one library error from the check.
- `Misc/stress/test_teleport_gl.py`: existing teleporter resource, transform,
  failure, and subview restoration regression passes.
- Linux build: `make -B -C Quake -j8` rebuilds all translation units with the
  default flags and no forced `ctype.h` include. `menu.c` now includes that
  header directly. The forced rebuild reports warnings in unchanged
  `stb_image.h`, `r_alias.c`, and `image.c`; the changed files compile cleanly.
  CI/Windows and macOS builds still require platform validation.

The 2026-10-13 follow-up reran the focused checks in GL 4.5 and GL 2.1 and the
loader/teleporter checks successfully. Loaded-map checks with full CSQC and
player-preview pixel validation also pass in both contexts: GL 4.5 exercises
`-fsaa 4` over conflicting config, and GL 2.1 exercises FXAA-first config order.
Concurrent llvmpipe engine runs timed out waiting for localhost RCON replies;
both checks pass when run sequentially. Use sequential engine checks on this
software-rendered setup.

After merging the five texture-refresh/cleanup commits through `7faa3703`, a
fresh isolated build of the combined source also passes the live texture-mode
harness with 4x MSAA and with FXAA preset 2. Each run checks 20 AA-state points
across texture/HUD changes, video restart, map changes, disconnect, and save
load, with unchanged saved gameplay and no renderer GL errors. This audit uses
`QSS_CFLAGS=-O0`; it validates integration rather than performance. The texture
callbacks remain intact and refresh runs before the frame's AA targets bind;
no renderer adjustment was required for the merge.

On this software driver, requests 2/4/6/8/16 applied 4/4/8/8/8 samples. Reporting
uses the queried sample count rather than assuming the requested count.
The loaded-map regression also exposed an existing scene-cache bug: the world
caustics uniform was uploaded while sky/water shaders were active. The upload
now runs only with the world shader bound.

The production review found and fixed three additional failure-path issues:

- A failed resolve-texture resize could leave old storage while publishing new
  dimensions. Texture storage must now succeed before dimensions are published;
  failure releases it, disables the affected postprocessing, and renders to the
  window until a request, size, or video restart permits a retry.
- Resolve-FBO creation could leave an offscreen framebuffer bound when the
  driver accepted no multisample count and no gamma pass was required. Allocation
  helpers now return with the default framebuffer bound on success and failure.
- Failure warnings could recursively render a partially constructed scene while
  disconnected. Cleanup and failure latching precede diagnostics, and warning
  output temporarily suppresses recursive screen updates.

The new texture-failure regression reproduced the stale-storage bug before the
fix and passes afterward. During CSQC pixel validation, SDL's offscreen driver
reported resized dimensions without resizing its default framebuffer. The test
still exercises larger scene attachments and connected video restart, then
returns to the original 640x480 size before checking window pixels. Physical
window-resize correctness requires the hardware coverage below.

A second production review reproduced and fixed two more issues:

- The framebuffer loader tried core names first on EXT-only contexts. A
  non-null address does not prove that the current context supports that API,
  as the [GLX procedure-address specification](https://registry.khronos.org/OpenGL/extensions/ARB/GLX_ARB_get_proc_address.txt)
  explains. The loader now selects core names for GL 3+/ARB support and EXT
  names for EXT support, and resets all framebuffer pointers on reinitialization.
- Resizing while MSAA and gamma were off, then enabling gamma inside a draw
  callback, could resize the color texture through the copy fallback while an
  inactive FBO retained its old depth/stencil dimensions. Reusing that FBO
  could clip output or fail completeness. Changing color storage now releases
  its dependent FBO and depth/stencil buffer so the next frame recreates them.

Both new regressions failed before their fixes and pass afterward. Attempting
an EXT-only Mesa context by disabling `GL_ARB_framebuffer_object` caused Mesa
to reject the advertised EXT multisample sample-count query. The engine fell
back to no MSAA; this forced configuration does not validate successful MSAA
on an actual EXT-only driver. The capability-loader simulation covers selection
and missing-function handling, while that hardware check remains outstanding.

A third production review reproduced a signed-ID bug in both scene read helpers.
They returned a framebuffer name as a signed `GLint` and used negative values
to mean no resolve was needed. A valid object name such as `0x80000001` therefore
left the resolved FBO bound for reading after a screenshot or texture copy.
The helper now returns a boolean and saves the previous name separately. A
real-GL test with a complete framebuffer using that name failed before the fix
and passes afterward for both readers, in GL 4.5 and GL 2.1 contexts. Mixed
read/draw target tests also verify that nested reads preserve their caller's
targets and do not resolve when reading a different framebuffer.

The expanded native-preview check also exposed a pre-existing `-noglsl` crash:
`MenuPreview_DrawQuad` called the unavailable `GL_UseProgramFunc` pointer.
The fixed-function quad now checks the pointer before clearing a shader
program. The same setup-menu test covers the compatibility renderer with
both GLSL and multitexture disabled, alongside the MSAA and gamma transitions.

A fourth production review reproduced another warning-reentrancy bug. During
signon or while disconnected, an unsupported-softemu warning in the final
postprocessing pass could trigger `Con_Printf`'s nested screen redraw. That
redraw overwrote the shared resolved texture before the outer frame presented
it. Render-time softemu warnings now suppress that redraw and restore the
caller's loading flag; the palette-LUT allocation warning uses the same helper.
The pixel regression failed before the fix and passes afterward in GL 4.5 and
GL 2.1, covering both unsupported configurations. The expanded engine checks
also pass video restart with an existing player preview and renderer fallbacks.

Run the focused check without game data:

```sh
SDL_VIDEODRIVER=offscreen python3 Misc/stress/test_live_fsaa.py
python3 Misc/stress/test_live_fsaa_loader.py
SDL_VIDEODRIVER=offscreen python3 Misc/stress/test_teleport_gl.py
```

If `/tmp` is full, first create `build/live-fsaa/tmp` and export
`TMPDIR="$PWD/build/live-fsaa/tmp"` before running builds or focused checks.
The teleporter harness also has an independent maintenance fix: it supplies the
occlusion-query capability and deletion-pointer stubs required by the existing
teleporter code. Keep that fix in its own commit.

Run the engine check with original Quake data (the test creates its own basedir
under `build/live-fsaa`, preserving the supplied pak files and user config):

```sh
SDL_VIDEODRIVER=offscreen python3 Misc/stress/test_live_fsaa_engine.py \
    Quake/quakespasm --paks /path/to/id1
```

Add `--fteqcc /path/to/fteqcc` for the full CSQC pixel regression. The test dumps
the engine's own CSQC declarations and compiles the fixture in its isolated basedir.

Remaining manual coverage: Windows and macOS hardware/drivers, actual macOS
gamma ramps, and physical resize/HiDPI/fullscreen/minimize transitions. A GL 2.1
compatibility-context pass does not prove an EXT-only
driver path. Representative hardware timedemo comparisons remain outstanding:
compare the baseline with 0x and 4x, both with motion blur/render scaling off
and with each enabled separately. Those effects each add a full-frame resolve
when they read an MSAA scene; include resolve cost and allocation hitches.
Check screenshots taken between frames on macOS and Wayland: the existing path
reads the window after presentation, whose backbuffer contents may be undefined.
Linux offscreen captures do not establish correctness on those platforms.

User-facing compatibility and memory notes are in
[`live-vid-fsaa-release-notes.md`](live-vid-fsaa-release-notes.md).
