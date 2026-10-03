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
  `gl_load24bit` since the textures were made (recorded at load) drops them at
  the next load. A model with any memory-backed texture (e.g. an embedded sky)
  is never retained.

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
