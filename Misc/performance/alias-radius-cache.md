# Alias outline/shell radius cache

2026-10-16, compared with `f081ac46`.

`R_CalculateAliasModelRadius` now retains its result in each MDL/MD3 surface's
frame descriptor. The first call uses the original calculation; subsequent
calls avoid the vertex scan. The descriptor belongs to the model's relocatable
cache block, starts at zero on load, and disappears when the model is freed.
The minimum returned radius is one, so zero unambiguously means uninitialized.
Storage costs four bytes per frame per surface.

IQM/MD5 results remain uncached because they depend on the current blended
bone pose. Other formats retain their existing behavior. Invalid entity frame
numbers use frame zero, matching `R_SetupAliasFrame`. Existing radius math,
MDL group first-pose behavior, outline widths, fades, and visibility rules are
preserved; this change does not introduce entity occlusion culling.

## Validation

- `make -C Quake -j4`: Linux release build passed.
- `python3 Misc/stress/test_alias_radius.py`: passed with AddressSanitizer,
  UndefinedBehaviorSanitizer, and leak checking. It compiles the production
  helper against real model types and checks exact results, cache hits without
  additional square roots, frame changes/groups, invalid frames, separate
  surfaces/models, cache relocation, reload at the same address, minimum and
  precomputed radii, and changing skeletal poses/weights at the same frame and
  bone-state address.
- Existing `test_outline_replay.py` and `test_alias_outline_instancing.py`
  passed.
- `test_live_texture_lifecycle.py` passed with the production binary, including
  model-cache flushes, video restarts, and game changes using offscreen GL.
- Four same-process old/new screenshot pairs were pixel-identical: paused
  `bravado` at player positions `781 444 104` and `840 904 128`, angles `0 0 0`,
  with stock assets and with isolated MDL copies whose radius metadata was
  cleared to exercise the fallback. Both paths see identical assets within
  each pair. These use `r_outline 5`, `r_outline_minpixels 0`, and 640x480
  offscreen GL. Console animation and fading notifications are disabled for
  the comparisons. They verify those MDL scenes, not a skeletal/MD3 visual
  fixture or the original desktop scene.

## Performance evidence and limits

A same-process microbenchmark compiled the old and new production C helpers
at `-O2`. Synthetic two-frame models use identical vertices for both paths.
Order alternates each repeat; the first repeat is discarded and medians use
seven retained repeats of 20,000 calls each. Results on this Linux host:

| Format | Vertices | Uncached per call | Cached per call |
| --- | ---: | ---: | ---: |
| MDL without precomputed radius | 128 | 417 ns | 7.0 ns |
| MDL without precomputed radius | 1,024 | 3,701 ns | 7.0 ns |
| MDL without precomputed radius | 8,192 | 31,789 ns | 7.0 ns |
| MD3 | 128 | 475 ns | 7.0 ns |
| MD3 | 1,024 | 4,495 ns | 6.7 ns |
| MD3 | 8,192 | 29,679 ns | 7.0 ns |

These are helper timings, not measured game FPS gains. Stock local MDLs
already supply positive bounding radii, unlike the slow-path-heavy asset set
in the original investigation. This host also uses Mesa llvmpipe software
OpenGL. A whole-frame paired run encountered severe stalls (including a
13.5-second frame) and is unsuitable for estimating an FPS improvement.
The previously reported 8% desktop gain has not been reproduced here.

Local scripts, the old/new single-binary toggle, raw helper samples, and live
artifacts are under `.codex-build/radius-cache/`. Benchmark instrumentation is
absent from the production sources and binary.
