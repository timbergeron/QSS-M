# Immortal Lock performance

Measured 2026-09-10 on an Apple M1 Pro, macOS 27.0, Release build, 1089x716
windowed, `-nosound`, isolated basedir, god/noclip/notarget.
Baseline source: `d722449f6`.

## Why this map is slow

`immortal.bsp` declares 16,162 entities and spawns 9,517 live edicts. Among
them are **185 `func_train`** (with 476 `path_corner`) and 335 `func_door`.
Trains move continuously, and every moving `MOVETYPE_PUSH` entity runs
`SV_PushMove`, which walks the entire edict array looking for things it might
shove. That is roughly 60 full scans of 9,517 edicts per server tick; at
`host_maxfps -150` it is ~90M edict visits per second.

A 3-second `sample` of the unmodified build put `SV_PushMove` at the top of the
whole process: **270 of 980 main-thread samples (27.6%)** — more than any
renderer symbol.

| main thread, baseline | samples | % |
| --- | ---: | ---: |
| server total (`Host_ServerFrame`) | 431 | 44% |
| — `SV_PushMove` candidate scan | 270 | 27.6% |
| — `SV_FindTouchedLeafs` (pusher relink) | 60 | 6.1% |
| — `PF_Find` → `PR_GetString` → `strcmp` | ~50 | 5% |
| — `SV_PresendClientDatagram` | 29 | 3.0% |
| render total | 432 | 44% |
| — particle draw blocked on the GL pack thread | 134 | 13.7% |
| — brush model draws | 51 | 5.2% |
| — `R_UploadLightmaps` → `memmove` | 44 | 4.5% |
| client (`CL_ReadFromServer`, parse) | 60 | 6% |

## The change: a pusher candidate cache

`sv_phys.c` now builds the candidate set once and reuses it across the pushers
in a tick, instead of rescanning every edict for every pusher.

The list is deliberately a **superset**. Both pusher loops still run the
original `movetype`/`free` tests on each entry, so a list that is stale in the
too-large direction cannot change behaviour. The cache's only failure mode is a
list that is *missing* a live candidate, so it is dropped by everything that can
turn a non-candidate into one:

- any call into QC (`PR_ExecuteProgram`) — covers `.movetype` assignment,
  `spawn`, `remove`, and the `putentityfieldstring`-style builtins;
- `ED_ClearEdict`, `ED_Free`, and `ED_Alloc`'s array-growth path;
- the top of every `SV_Physics` frame — covers console commands, savegame
  loads and map spawn, none of which run inside a server frame.

`SV_PushMove` and `SV_PushMoveAngles` are reachable only from
`SV_Physics_Pusher`, never from QC, so the cache can never be revalidated while
progs code is running. If it is dropped part way through a pusher's loop, the
iterator falls back to the plain scan from the next edict number onwards, so
the visited set and its order stay identical to the uncached code.

The iterator that walks the list has a second failure mode of its own --- a
wrong order, or an edict number that does not match the entity it is handed
with --- which is why the verifier checks the full contract rather than just
membership. See Verification.

`sv_pushcache` — `0` restores the original scan, `1` (default) uses the cache,
`2` verifies it against a plain scan on every use, `3` also forces the fallback
path.

The `movetype` test also now runs before the `free` test in both loops. Both
conditions are side-effect free and both `continue`, so the order is
behaviour-neutral; `free` is at edict offset 0 and `v.movetype` at 280, so
testing `movetype` first skips a second cache line for every entity the
movetype filter rejects.

## Results

Measured against a **stock build of `d722449f6`**, not against `sv_pushcache 0`.
That distinction matters: an early version of the iterator made the uncached
path 17-18% *slower* than the original loop, because the loop body is only a
few instructions and the iterator added branches and a thread-local `qcvm`
dereference per entity. Comparing the two modes of one binary hid that and
inflated the apparent gain.

Each figure is one process at a fixed viewpoint, 3-4 samples per arm, FPS from
the engine's own `netfps_probe`. `host_maxfps -150`.

| Viewpoint | stock `d722449f6` | `sv_pushcache 0` | `sv_pushcache 1` | 95th pct floor |
| --- | ---: | ---: | ---: | --- |
| Entrance | 210.0 | 204.6 (-2.6%) | **244.4 (+16.4%)** | 125.1 -> 176.6 (+41.2%) |
| Interior | 128.7 | 126.5 (-1.7%) | **208.1 (+61.7%)** | 85.9 -> 132.9 (+54.7%) |

`sv_pushcache 0` now tracks stock to within a few percent, so it is usable as a
behavioural fallback and a rough baseline. Getting it there needed the scan's
loop-invariant state hoisted into the iterator: `qcvm->edicts` and `edict_size`
are fixed for the level, and `qcvm->num_edicts` only ever grows (`ED_Alloc`),
so refreshing the bound once the scan reaches it is exactly equivalent to
vanilla's re-read-every-iteration while keeping the thread-local out of the
inner loop. `SV_PushIter_Next` is `FUNC_ALWAYSINLINE`, and the verifier takes
scalars rather than `pushiter_t *` so the iterator state can stay in registers.

Within a single process the harness is precise --- the stock build's two
(identical) arms agree to 0.5%. Between processes there is a few percent of
drift, because the map is live. The +16% / +62% signal is far above that.

Earlier in the investigation the interior gain measured anywhere from +33% to
+62% across runs. The spread is the map, not the harness: how many `func_train`
happen to be moving varies over time, and that is exactly what the cached path
stops paying for. With the cache on, the interior figure is stable run to run
(206-212); with it off it swings (126-158).

Camera positions:

```text
setpos -2960 272 1596 0 0 0
setpos 2541 648 894 0 90 0
```

At `host_maxfps 0` (72 Hz server, so half the pusher work to begin with) the
gain was about +9% at both viewpoints.

## Verification

`sv_pushcache` has four levels: `0` restores the original scan, `1` (default)
uses the cache, `2` verifies the cache against a plain scan on every use, and
`3` additionally drops the cache every 8th yield so the fallback path runs
constantly.

### What the verifier checks

Membership alone is not enough. `SV_PushMove`'s elevator fix tests
`e <= svs.maxclients`, so an edict number silently replaced by a position in
the cache would change behaviour while every entity still got visited. The
verifier therefore walks a shadow plain scan in step with the iterator and
compares the whole contract: same set, same order, same edict number attached
to the same entity, and both finishing together.

**vkQuake has exactly that bug.** With `sv_fastpushmove 1` its `e` is the cache
index (`Quake/sv_phys.c:522`, `check = pushable_ent_cache[e]`) but is still
compared against `svs.maxclients` as if it were an edict number at line 609.
It is latent only because their `sv_gameplayfix_elevators` defaults to `2`,
where the `>= 2.f` branch short-circuits before `e` is read. At `1` it is wrong.
Ours stores edict numbers and derives the pointer from the number, so the two
cannot diverge.

### Negative controls

A verifier that never fires proves nothing, so both failure classes were
deliberately introduced and confirmed to be caught:

| Injected fault | Reported |
| --- | --- |
| Drop every 3rd candidate from the rebuild | `wrong entity: expected edict 1911, got 1912 (item_shells)` |
| Report the cache position instead of the edict number (vkQuake's bug) | `wrong edict number: expected edict 1, got 0 (player)` |

The second fires on the player, which is precisely the entity the elevator fix
keys on. Note that removing only the `PR_ExecuteProgram` hook is *not* a useful
control: the per-frame invalidation caps the exposure window at a single tick,
so it does not reliably produce a divergence.

### Clean runs

- Modes 2 and 3, immortal plus `start`, `e1m1`, `e1m2`, `e1m3`, `dm4`, `e4m2`,
  with movement, jumping and firing to force gibs, corpses, doors and plats
  (which exercises spawn, free and slot reuse continuously): **0 warnings**.
- **Rotating pushers**: peril3.0's progs requests `DP_SV_ROTATINGBMODEL`, so
  `rotatingbmodelmode` is `ROTATINGBMODEL_ENGINE_PUSH` there. `SV_PushMove`
  routes to `SV_PushMoveAngles` for any `MOVETYPE_PUSH` entity with avelocity
  *before* the zero-velocity early-out, so setting `edict N avelocity "0 60 0"`
  over rcon forces every door and plat in a map through the rotating loop every
  tick. A temporary probe confirmed the path really executed (>31,000
  `SV_PushMoveAngles` calls on `func_bob` across dock, hydrodam, industry,
  marsh and qstation) with zero "DP_SV_ROTATINGBMODEL is not enabled" warnings.
  Modes 2 and 3: **0 warnings**.
- The rotating loop rejects `MOVETYPE_ANGLENOCLIP` as well. The cache is built
  with the *looser* rule (`SV_IsPushCandidate`) and each loop applies its own
  stricter one, so `loop-accepts(e) => SV_IsPushCandidate(e) => e is in the
  cache` holds for both loops. Nothing but the iteration source and the
  movetype/free test order changed in either loop; riders, obstruction,
  rollback and `blocked` handling are byte-identical.
- Ordinary maps are unaffected: e1m1 at two viewpoints, 165 edicts, on vs off
  is +0.6% / -0.8%, inside noise, with identical frame times.
- Debug and Release both build clean; `git diff --check` passes.
- `PR_ClearProgs` frees the buffer, and every qcvm teardown goes through it.

### C-side writes

The opcode argument covers QC (`pr_exec.c` implements only the classic 66
opcodes, so `OP_ADDRESS` is the sole source of a `STOREP` pointer). The engine's
own writes were audited separately: 19 `v.movetype =` sites and 5 `->free =`
sites. Every one is either

- inside a QC call --- the `putentityfieldstring`-style builtin at
  `pr_ext.c:4619` writes an arbitrary field through `ED_ParseEpair` --- and so
  covered by the `PR_ExecuteProgram` hook; or
- a console command, map spawn, or savegame load. `Cbuf_Execute` runs at
  `host.c:2423`, *before* the `Host_ServerFrame` block in the same function, and
  the only other in-game call site (`Host_AutoLoad`) is reached solely from
  `Host_Changelevel_f` / `Host_Restart_f`. None can run inside `SV_Physics`, so
  the frame-entry invalidation covers them. Savegame load additionally goes
  through `Host_ClearMemory` -> `PR_ClearProgs`, which destroys the cache outright.

### Known gap

None of the above proves behaviour under a mod that changes an *existing*
entity's movetype to a pushable one from inside a callback during a scan. That
case is handled by construction (QC entry invalidates, the iterator falls back
to a live plain scan) and mode 3 exercises the fallback machinery continuously
against the shadow scan, but it has not been reproduced with real mod QC.

## What is next, measured after the change

Re-profiling at the interior viewpoint puts the pusher scan at **8.5% +2.2%
rebuild**, down from 27.6%. The new leaders:

- `Sky_ProcessEntities` 7.7% — walks every visible brush entity and all of its
  surfaces each frame hunting for `SURF_DRAWSKY`. A per-model "has sky
  surfaces" flag computed at load would skip almost all of it.
- `SV_FindTouchedLeafs` 6.8% — `SV_LinkEdict` for the moving trains, once per
  tick each, down a deep BSP tree.
- `PF_Find` → `PR_GetString` → `strcmp` 6.8% — mod code calling `find()` over
  all 9,517 edicts. A `string_t` equality fast path before `PR_GetString`
  would help.
- lightmap upload, `memmove` 4.6% + `R_BuildLightMapForState` 3.3%, plus
  `glTexSubImage2D` on the GL thread — this map has 424 `light_flame_small_white`
  and 253 `light_tubelight`, so animated light styles dirty lightmaps constantly.

## Harness

`/tmp/qssm-immortal-bench2/` — `bench3.py` (interleaved A/B via `netfps_probe`),
`validate.py` (`sv_pushcache 2` across maps), `prof.py` (`sample` at a fixed
viewpoint). Two traps worth remembering: `-condebug` writes `qconsole.log` next
to the executable, not into `-basedir`; and `qconsole.log` is written with raw
`write(2)`, so unlike stdout it is never buffer-delayed.
