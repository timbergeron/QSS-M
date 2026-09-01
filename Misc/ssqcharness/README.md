# Rotating-bmodel SSQC harness

This directory contains the content inventory and regression harness for the
QuakeEX rotating-bmodel ownership cleanup.  It deliberately does not claim to
match KEX rider, `blocked`, or rollback behavior: no official KEX executable
was available for reference testing.  The existing external-angle relink path
is therefore preserved, while Target B/C and shadow-angle state remain
deferred.

## Preserved ownership matrix

| VM/state | Angle-aware collision | Integration owner | Rotation query |
|---|---|---|---|
| Ordinary QC, no query | no | legacy/none | not made |
| Ordinary QC, accepted query | yes | engine | true |
| Ordinary QC, blocked query | no | legacy/none | false |
| QuakeEX QC | yes | external QC | false |
| CSQC | yes | external/server | not relevant |

`qexlogic` remains the general rerelease classification.  A QuakeEX query
cannot opt stock rerelease QC into engine integration, even during a passive
extension sweep.

## Shipped-content characterization

`inspect_rotators.py` reads PAK directories, BSP entity/model/face lumps, and
produces `rerelease_rotators.md`.  The checked report covers the locally
installed MG1 and MG3 packs by SHA-256 and records all 75
`rotate_object_continuously` entities, including origins and runtime `pos2`
origins, flags, `avelocity`, delay, model bounds, face-derived dimensions, and
target relationships.

The scan found 55 solid obstacle rotators and 20 non-solid decorative
rotators.  It found no solid decorative, start-off, mixed linear/angular, or
source-confirmed crushing rotators, and no models matching the report's
conservative rideable-platform heuristic.  Those negative results mean the
shipped BSPs do not provide a defensible KEX rider/rollback target.

Regenerate the report with:

```sh
python3 Misc/ssqcharness/inspect_rotators.py \
  MG1=/path/to/mg1/pak0.pak MG3=/path/to/mg3/pak0.pak \
  -o Misc/ssqcharness/rerelease_rotators.md
```

The rerelease QC source used for local characterization was
`/Users/timbergeron/codedev/NQW/src/rotate.qc`, SHA-256
`d6739177279253a0297b69f9338fedb01906f7ab692ec67b60058f1ca2ba9060`.
It confirms that continuous and tweened rotation increment and wrap the
current angle.  Stopped objects retain nominal nonzero `avelocity`; tweening
scales only the QC-produced delta by `speed`.  Consequently, an engine
rollback persists into the next QC tick and `avelocity * movetime` cannot
reconstruct actual rerelease motion.

## Regression harness

`build_test_bsp.py` generates a deterministic collision-only BSP, so the test
does not depend on a platform-specific map compiler.  Its six brush submodels
represent a thin obstacle, rideable platform, blocking wall, mixed-motion
platform, stopped rotator, and tweened rotator.  `world.qc` spawns and
instruments them.

The ordinary and QuakeEX QC variants cover:

- accepted, rejected, unqueried, and passive-enumeration extension ordering;
- precache-warning and effects-state transactionality;
- engine-owned and externally owned single angle integration;
- rotated point traces and rotational broadphase relinking;
- pure linear and mixed linear/angular movement with advancing `ltime`;
- ordinary DP rider origin/orientation, trigger touches, `blocked`, and
  rollback behavior;
- external-angle no-carry behavior, stopped nominal `avelocity`, and a
  tween-scaled QC delta.

Run both server modes with:

```sh
python3 Misc/ssqcharness/run_harness.py \
  --bin /path/to/QSS-M.app/Contents/MacOS/QSS-M \
  --basedir /path/to/quake \
  --fteqcc /path/to/fteqcc \
  --server-mode both
```

The runner uses a temporary game directory, restores both supported `id1`
config paths byte-for-byte, and removes only its own temporary directory.  It
writes commands, QC compiler output, per-case logs, hashes, executable
timestamps, and cleanup verification to `artifacts/`.

Smoke-load every QSS-M MG1/MG3 map containing a shipped rotator with:

```sh
python3 Misc/ssqcharness/smoke_rerelease.py \
  --bin /path/to/QSS-M.app/Contents/MacOS/QSS-M \
  --basedir /path/to/quake
```

This is an engine/content smoke test, not a substitute for observing those
maps in the official KEX executable.  The script restores MG1/MG3 configs and
writes its own hashes and per-map logs to `artifacts/`.

## Deferred reference work

An official KEX run is still required to independently observe rotated
collision, rider translation/orientation, obstruction and `blocked`, rollback,
stopped behavior, and any mixed-motion behavior.  Only then should the code
select Target A, B, or C.  Until then, there is no hybrid QuakeEX opt-in and no
external-angle shadow/delta lifecycle state.
