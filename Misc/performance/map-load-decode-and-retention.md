# Map load: parallel image decode and item-model texture retention

With `developer 1` a level load prints where the time goes (`SV_SpawnServer ...`,
`Mod_LoadTextures ... imageload`, `ED_LoadFromFile`, `R_NewMap ...`,
`connect timing: ...`). On a retextured map the profile showed two big, avoidable
pieces:

1. **Serial image decode.** `Mod_LoadTextures` asked `Image_LoadImage` for each
   replacement texture in turn, so PNG decode (stb_image) ran on the main thread
   while the texture workers sat idle. `start`: ~690 ms of a ~1.4 s spawn.
2. **Item models reloaded on every map.** The `maps/b_*.bsp` ammo/health models
   were freed by `Mod_ClearAll` and then re-found, re-decoded and re-uploaded
   (~250 ms per map change), although every map wants the same textures.

## What changed

* `Image_Prefetch*` (`image.c`): a map queues the names it is about to request.
  The first request for an unscanned name locates and reads a window of files
  serially (the filesystem layer isn't thread safe), decodes the window across the
  texture workers (`TexMgr_ParallelFor`), and answers the following
  `Image_LoadImage` calls from that table. Only PNG/JPEG are decoded this way,
  and names that weren't queued, or any other format, take the ordinary path, so
  results are unchanged. Windows are capped at 24 textures / 48 MB decoded; an image that doesn't fit the
  budget is left to the ordinary loader, and stb's fixed Huffman tables are
  initialised before the workers start.
  Used for `Mod_LoadTextures` (gl_load24bit 1) and the six skybox faces.
* `Image_LoadImage` was split into `Image_Locate` (which file wins) and the
  loaders, so the prefetch scan and the normal path share one search order.
* `Mod_RetainTextures` (`gl_model.c`): `maps/b_*.bsp` models keep their GL
  textures when unloaded. The next load claims the ones it needs
  (`Mod_ClaimRetainedTextures`, with the grass analysis that was made from the
  pixels) and frees the rest. `Mod_ResetAll` frees everything, and a change to
  `gl_load24bit` since the textures were made (recorded at load) drops them during
  the live refresh, or at the next load if changed before initialization.
  A model with any memory-backed texture (e.g. an embedded sky)
  is never retained.

## Live texture mode changes

Changing `gl_load24bit` now refreshes loaded textures before the next rendered
frame, including transitions between `0`, `1`, and `2`. BSP texture records,
geometry, collision, entities and lightmaps stay in place. Render caches are
invalidated after waiting for their workers; inline brush models share the
parent's refreshed textures. Cached alias models reload through the existing
loader, and sprites retain their frame records and animation intervals.
Models record the effective image mode when loaded. A startup/config change
followed by a map load therefore skips a redundant refresh, and coalesced
changes ending in the already loaded mode do not upload images again. Cached
alias skins can refresh independently without rebuilding fresh brush textures.
Retained item textures keep their previous mode until the brush loader has
checked it; a mode change combined with a map load cannot reuse missing glows.

The HUD also refreshes when its effective replacement setting changes, including
changes to `gl_load24bit_hud`. Only draw-cache textures are purged: particle
textures, the selected skybox and the separate levelshot cache remain valid.
The weapon wheel's generated axe icon follows model skin replacements even
with HUD replacements disabled, so its cache is invalidated independently.
Scripted particles use their own texture records when UI images have the same
name, so freeing the UI cache cannot change active particles' texture pointers.
Classic sky layer buffers belong to their textures and are freed on refresh.
Temporary file decoding in `TexMgr_LoadImage` is included in its hunk rewind,
so repeated TGA/HUD changes do not accumulate decoded image buffers.
Alias mesh rebuilds also release the previous CPU vertex/index arrays when
VBOs are disabled. The texture-pointer tool is reset before texture records
can be reused, and redraws triggered by console output are suppressed until
the refresh finishes. Truncated BSP pixel runs keep their padded in-memory
source so a later video restart cannot read adjacent BSP data as pixels.

There can be a loading hitch while replacements are decoded and uploaded, but
the map does not restart and the client does not reconnect. The graphics menu's
external-texture toggle uses the same cvar callback. On large maps with external
texture packs, this may pause rendering for a second or longer.

`python3 Misc/stress/test_live_texture_mode.py <binary> <id1-assets>` runs the
real renderer with SDL's offscreen driver. It checks original/replacement image
selection, skins, sprites, HUD, skybox preservation, repeated switches, stable
hunk usage and saved gameplay state, followed by a video restart, map changes,
retained item glows, disconnected menu refreshes and loading a saved game.
Logs, screenshots and saves are kept in `.codex-build/live-textures/`.
`--legacy` runs the same checks with VBOs and parallel preparation disabled.
`--sprite-groups` adds indexed and RGBA sprite groups, angled frames and
non-power-of-two replacements with forced padding; original sprite mip hashes
must remain identical across refreshes.
The restart check enables mip hashes and requires world/model uploads, so a
locked or unchanged video mode cannot make the check pass without a restart.
`Misc/stress/test_alias_mesh_reload.py` runs 1,000 mesh rebuilds under sanitizers
and verifies CPU-buffer ownership and transitions to/from VBOs.
`Misc/stress/test_live_texture_wheel.py` checks live axe-icon pixels with HUD
replacements disabled while the wheel stays open.
`Misc/stress/test_particle_texture_lifetime.py` exercises matching particle/UI
image names through 1,000 UI record reuse cycles under ASan/UBSan.
`Misc/stress/test_live_texture_lifecycle.py` checks classic sky and fog state,
alias-cache flushes, pending video restarts, and game-directory changes.
Its `--sanitizers` option fails on ASan/UBSan errors when given an instrumented
engine; full-engine exit leak checks are disabled. The focused particle and
mesh tests also run with leak checking enabled.
`Misc/stress/test_draw_pic_alignment.py` checks real picture draws from both
four-byte and pointer-aligned cache records, including their UVs and vertices.

## Switches

| | |
|---|---|
| `-noparalleltextureprep` / `tex_workers 1` | also disables the decode prefetch |
| `mod_retain_textures 0` | frees item-model textures at map change as before |

## Measured (Windows, RTX 4070, median of 3, warm OS file cache)

| | before | after |
|---|---|---|
| first load `dm3` | 2087 ms | 1554 ms |
| first load `start` | 1833 ms | 1406 ms |
| first load `ad_tears` (84 MB bsp) | 2660 ms | 2505 ms |
| `dm3` -> `dm4` | 919 ms | 548 ms |
| `dm3` -> `dm3` | 1636 ms | 918 ms |
| `start` -> `dm3` | 1761 ms | 1224 ms |

Sum of world parse + entity spawn + client connect, from the `developer 1` lines.

## Verifying

`+tex_verify 1 +developer 1` prints a `texhash` line per texture mip. Sorted
`texhash` output is identical with and without `-noparalleltextureprep` for
`start` and `dm3`. `imagelist` after `dm3` -> `dm4` is identical with
`mod_retain_textures 1` and `0`, also across a `vid_restart` and a
`gl_load24bit 0` between the maps.

## Not done

Alias model skins (`Mod_LoadAllSkins`) still decode and upload one at a time:
~250 ms on a first `dm3` load, but they persist across maps once loaded.
