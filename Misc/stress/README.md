# QSS-M stress tester / crash finder

`qssm_stress.py` drives a real QSS-M build through randomized sessions and
reports anything that crashes, wedges, or dies with a fatal engine error.  It
covers the things a player actually does -- connecting to a server, changing
maps, playing, walking the menus, replaying demos -- plus argument fuzzing over
every registered console command and cvar.

## Requirements

* a QSS-M build with the optional stress hooks re-added (`Host_StressInit` in
  `Misc/stress/host_stress.c`); production targets currently omit them
* `pak0.pak` (+ `pak1.pak` for the registered maps); found automatically in
  `~/Desktop/qssm/id1/paks`, or pass `--paks <dir>`
* loose base assets such as `gfx.wad`; the harness auto-detects
  `~/Desktop/qssm/id1/paktest`, or pass `--loose-root <game-root>`

## macOS harness status

The stock Xcode and Makefile targets do not currently define `QSSM_STRESS` or
compile `Misc/stress/host_stress.c`.  Normal macOS builds therefore keep their
existing command surface, but cannot run the in-process stress channel.  A
dedicated macOS stress target must add that source file and define before using
this harness; verify it with `strings QSS-M | grep -E
'STRESS_READY|_stress_status'`.

## Run a live Windows client probe

The production Windows Visual Studio project currently does not compile the
stress hooks. To use this probe again, re-add `Misc/stress/host_stress.c` and
the `QSSM_STRESS` define to a dedicated local stress target. Keep that target
separate from production builds; an unoptimised `Debug` build tells you
nothing useful about frame time.

Then drive the real client through map loading, exact `setpos` commands, and
frame profiling:

```powershell
python Misc/stress/live_client_probe.py `
  --bin Windows/VisualStudio/.codex-build/prof-out/quakespasm-sdl2.exe `
  --basedir "$env:USERPROFILE/Desktop/qssm" `
  --output Misc/stress/tmp/ad-tears-live
```

The probe writes `report.json` under the output directory and tails the
client's `qconsole.log` beside the executable.
Unlike command-line `+setpos`, negative coordinates are delivered after
startup through the append-only stress file and are not truncated by the
engine's command-line parser.

### Curated alias-instancing demos

`demo_alias_probe.py` defaults to the `best-data` suite in
`alias_instancing_demos.json`. These three primary fixtures produced the best
fair A/B results during the alias-instancing work:

| Tier | Demo | Map | Observed gain |
| --- | --- | --- | ---: |
| primary | `bravado_01-21-2023-154438` | bravado | 8.675% |
| primary | `ctf3m1-12-19-2021-707pm` | ctf3m1 | 4.033% |
| primary | `pound_12-13-2022-201443` | pound | 1.640% |
| secondary | `ctf3m9-11-4-2021-624pm` | ctf3m9 | 1.051% |
| secondary | `kaboom-12-3-2021-745pm` | kaboom | 1.266% |

The gains are historical measurements from fair Windows A/B runs, not
performance guarantees. Run the primary regression set with:

```powershell
python Misc/stress/demo_alias_probe.py `
  --bin path/to/stress-enabled/quakespasm-sdl2.exe `
  --basedir "$env:USERPROFILE/Desktop/qssm" `
  --output Misc/stress/tmp/alias-best-data
```

Use `--suite extended` to include the two lower-signal valid fixtures
(`ctf3m9` and `kaboom`), or use repeated `--demo name` arguments to run a
specific fixture.

### Where the harness attaches to the engine

The following table documents the archived harness boundary. The production
player build contains none of it.

| Site | File | What it does |
| --- | --- | --- |
| `Host_StressInit ()` | `host.c`, end of `Host_Init` | Parses `-stress <path>`, registers the `_stress_*` commands, prints `STRESS_READY`. |
| `Host_StressPoll ()` | `host.c`, in `_Host_Frame` before `Cbuf_Execute` | Drains the script file (throttled to 50Hz) and samples the frame-time profiler. Being on the host thread is what makes key/menu/console commands safe to execute. |
| `QSSM_STRESS` | Dedicated local stress target | Must be defined explicitly when re-enabling the archived command channel. |
| `host_stress.c` | Dedicated local stress target | Re-add manually for a future harness build; it is not part of the production project. |
| `NET_Address_RunSelfTests ()` | `net_main.c`, `NET_Init` | Runs in all normal non-`NDEBUG` builds. |

`Host_StressPumpModal`, `Host_StressNoteParse` and `Host_StressCoverage` are
defined but **not currently called from the engine** on this platform.  They
exist for the macOS lanes; do not assume modal-dialog answering or parser
coverage works in a Windows run until those call sites are added.

### Instrumentation counters

**Archived instrumentation.**  `QSSM_STRESS` gave the command channel and the
frame-time profiler, while `QSSM_RENDERSTATS` additionally enabled the
`STRESS_PROF_CACHE` line, and requires the renderer counter patch to be applied
(the `rs_*` globals and their `RSC_STAT`/`RS_STAT` call sites).  That patch is
deliberately not part of the engine history: it is applied when there is
something to measure and dropped again afterwards.

When the archived hooks are re-enabled, the stress target gives you:

```
/p:QSSMStress=1                            -> STRESS_PROF (frame percentiles)
/p:QSSMStress=1 + counter patch + define   -> STRESS_PROF and STRESS_PROF_CACHE
```

The archived renderer-counter patch declares the `rs_*` globals behind its
stress define, and every update goes through a macro:

```c
RSC_STAT (rs_scenecache_builds++);          /* scene cache */
RS_STAT  (rs_bshadow_indices += n);         /* alias + shadow */
RS_ALIAS_DRAW (RS_ALIAS_SHADOW, numtris);   /* per-pass draw + triangle count */
```

Add new counters the same way, and keep them at **per-job or per-frame
granularity**.  An earlier revision timed every visited surface, which put
~190k `Sys_DoubleTime` calls per rebuild into the worker's hot loop -- real
shipping overhead, and because only one side of the A/B paid it, the comparison
was biased by 8-10% in the fix's favour.

What the counters cover:

* **Scene cache** -- `builds`, `stylebuilds`, `uploads`/`uploadkb`/`uploadms`,
  `blocks`/`blockms`, `skyscanms`, `workerwallms`, `lightmapwallms`,
  `lmscanned`/`lmbuilt`, `stylejobs`, `stylebusy`, and the main-thread phase
  timers `visms`/`queuems`/`drawms`.
* **Alias** -- `edicts`, `aents`, and the instancing-bucket triple
  `akeysw`/`adkey`/`adModel`.  The key is `(model, pose1, pose2)`, because the
  pose attrib pointers are offset by `lerpdata.pose1/pose2`; keying on the model
  alone badly understates the bucket count (8 models vs 31-36 real keys).
* **Draw calls per pass** -- `dcMain`/`dcShadow`/`dcOutline`/`dcShell` counted
  at the `glDrawElements` site, with matching `trMain`/`trShadow`/... triangle
  totals.  Note `rs_aliaspasses` (johnfitz) adds `numtris` and so measures
  triangle passes, *not* draws.
* **Brush shadows** -- `bsEnts`, `bsSurfs`, `bsVerts`, `bsBuf` (how many used
  the index cache) and `bsIdx`.  `bsIdx` is computed identically on both the
  immediate and buffered paths, so it is the parity check when changing that
  code -- though it proves index *count* parity, not that the silhouettes match.
* **Particles** -- `partms` and `partcount`.
* **Shadow culling** -- `scullA`/`scullD`.

### Diagnostic cvars

These exist to isolate costs, not to be set by players.  Several deliberately
render incorrectly.

| Cvar | Meaning |
| --- | --- |
| `r_shadows_buffered` | `1` indexed brush shadows (shipping), `0` the original immediate-mode path.  Same geometry either way, so this is the honest A/B and the lever for a pixel diff. |
| `r_shadows_maxdist` | `0` off (default).  Positive values skip alias and brush shadows beyond that many units, measured to the entity's world-space bounds. A quality tradeoff, not a free win. |
| `r_alias_skip` | `1` replaces per-entity lighting setup with a constant.  `QSSM_STRESS` only. |

### Measuring frame time

Do not use `r_speeds` for this.  It times only `R_RenderView`, so it misses
present and the scene-cache index upload that lands on the main thread; it
reports whole milliseconds; and it prints a line every frame, so under
`-condebug` the measurement writes a file per frame and changes what it is
measuring.  Early probe runs that used it produced numbers that varied by 50x
between repeats of the same test.

Use `_stress_frameprof <seconds> <label>` instead.  It samples the whole frame
period once per frame, buffers the samples, and prints two lines at the end:

```
STRESS_PROF label=... frames=... fps=... min/p50/p90/p99/max=... over16=... over33=...
STRESS_PROF_CACHE label=... builds=... stylebuilds=... uploads=... uploadkb=...
                  uploadms=... blocks=... blockms=... skyscanms=... workerms=...
```

The `_CACHE` line comes from the `rs_scenecache_*` counters in `r_world.c`.
`builds` is scene-cache rebuilds queued, `stylebuilds` how many of those a
lightstyle animation tick forced, `workerms` the worker thread's build time,
`uploadms` the main-thread EBO upload, and `blockms` how long the render
thread waited on the worker.

The probe sets `host_maxfps 0` and `vid_vsync 0` so frame time reflects work
rather than the frame limiter, and it interleaves repeats across variants so
warm-up and clock drift do not land on whichever variant ran first.  Always
run `--repeats 3` or more and discard repeat 0; the first pass is warm-up.

Useful options:

* `--motion still|sweep|storm|walk` -- how the viewpoint moves while sampling.
  `sweep` teleports along X to churn the PVS, but each `setpos` is a console
  command with its own cost, so `storm` (re-`setpos` to the *same* spot at the
  same rate) is its control.  They measured the same, which is how we found
  that an early "motion" result was measuring command traffic and not PVS
  churn at all.  `walk` holds `+forward` and is the only mode whose movement
  matches a player's, but it drifts and can walk into a wall, so treat its
  numbers as indicative rather than controlled.
* `--cores N` -- restrict the client to N CPUs.  The scene cache offloads
  rebuilds to a worker thread, so on a machine with spare cores their cost
  hides; `--cores 2` is how you find out what happens when the worker cannot
  keep up.
* `--width/--height` -- match the machine's real resolution.  A 1280x720
  window and a 3840x2160 one are different tests.  Resolution insensitivity is
  evidence against a fill bottleneck only; it says nothing about vertex work,
  driver submission or synchronisation.
* `--position NAME` (repeatable) -- restrict to named positions.  Not just for
  speed: the scene-cache pool accumulates, so visiting two distant spots in one
  session inflates the lightstyle sweep's cache union and makes a single-spot
  cost look worse than it is.  `--position tears_low` alone measured 856ms of
  worker time where the two-position run reported 2647ms.
* `--variant NAME` (repeatable) -- restrict to named variants.  Variants are
  interleaved within each repeat, so warm-up and clock drift hit every cell
  equally instead of loading onto whichever ran first.
* `--repeats N`, `--sample-seconds N` -- see the drift warning above.

`STRESS_PROF_CACHE` reports `stylejobs` (lightmap-only refreshes performed) and
`stylebusy` (a refresh was offered while the worker was still busy).  A
non-zero `stylebusy` means the worker cannot keep up with the 10Hz tick and
lighting is updating more slowly than it should -- watch it whenever changing
the sweep's cost.  It counts *per-frame rejected offers*, not distinct dropped
ticks: at 300fps one busy job rejects many frames' worth.

### Traps this harness exists to avoid

Every one of these produced a wrong conclusion that had to be retracted.

**The machine drifts ~30% between sessions.**  The same cell measured
`workerwallms` 3325 in one session and 2504 in another.  Never compare numbers
across runs; only within-run, interleaved A/B.  Always `--repeats 3` or more
and discard repeat 0 -- it is warm-up, and is routinely 20-40% off the others.

**Variants must be hermetic.**  They run against one long-lived client, so a
variant that only sets what it *disables* leaves that setting applied for
everything after it.  This silently turned shadows off for most of an entity
bisect and made it look like no single feature mattered, when shadows were
actually ~30% of the frame.  `_entvariant()` in the probe restates every cvar
in its group for exactly this reason -- follow that pattern for new groups.

**Disabling a subsystem measures an upper bound, not the cost of its
submission.**  `r_shadows_bmodels 0` also removes lighting queries, ground
checks, matrices, rasterization and blending.  To size an optimisation, compare
optimised-vs-unoptimised with the feature still *on*, and compute recovery from
frame time:

    recovery = (T_before - T_after) / (T_before - T_off)

**Console commands are not free.**  A `setpos` sweep looked like PVS churn
costing 20ms p99, until re-`setpos`ing to the *same* spot at the same rate
reproduced it exactly.  `--motion storm` is that control; keep it.

**Entity counts vary even standing still** (203-246 between samples in
`ad_tears`).  A sample whose `edicts`/`dc` counts diverge sharply from its
siblings is not comparable -- check them before trusting a frame-time delta.

**Watch `printf` arity.**  The `STRESS_PROF_CACHE` format string and its
argument list are edited together; a mismatch compiles fine and silently prints
garbage for every field after the break.

The current engine boundary covers script-driven lifecycle, console, menu,
key, and character stress.  Parser-exact, server-message injection, and
raw-datagram lanes advertise their capability status at boot and are skipped
until their owning parser/network modules are wired to the corresponding
stress seam.

Every run gets its own throwaway `-basedir`, isolated HOME/TMPDIR, and working
directory.  PAKs and loose assets are copied into the sandbox rather than symlinked, so
file mutation scenarios cannot modify the real Quake install.  Windows will
open and close repeatedly while a campaign runs.

By default, run one campaign at a time. Each run launches a real GL window, and
two independent campaigns can starve each other for window/GL resources. The
opt-in `--jobs N` mode is different: it gives each worker its own result tree,
temporary home, port range, seed, and corpus staging directory. Use it for
throughput and scheduling diversity, not synchronized multiplayer. If every
worker fails to boot with an empty `console.log`, suspect concurrency or stale
`QSS-M` processes before suspecting the engine, and check `pgrep -x QSS-M`.

Each worker passes its selected port on the QSS-M command line, before the
engine creates its network sockets; the matching `port` command remains in
`autoexec.cfg` for later restarts and replayed configurations. If a worker
finds its port occupied, it records a `port_retry` event, selects another
worker-local port, and retries before reporting a boot failure.

The results directory also has an advisory OS lock; a second campaign using the
same `--results` path exits before launching an engine.

### macOS harness use

After creating a dedicated stress-enabled target as described above, point
`--bin` at the executable inside its app bundle.  An Xcode intermediate binary
does not carry the app's embedded SDL2 framework and will fail at boot with a
`dyld` `Library not loaded: @rpath/SDL2.framework` error.

## Usage

```bash
./qssm_stress.py --list                       # scenarios
./qssm_stress.py                              # one run of each scenario
./qssm_stress.py --minutes 30 --iterations 20 # a real campaign
./qssm_stress.py --scenario menu --runs 5 --seed 1234
./qssm_stress.py --stress-level smoke
./qssm_stress.py --stress-level deep --seed 1234
./qssm_stress.py --stress-level soak --contain
./qssm_stress.py --replay tmp/results/0003-menu/repro.txt
./qssm_stress.py --minimize tmp/results/0003-menu/repro.txt
./qssm_stress.py --regress tmp/results              # replay saved findings
./qssm_stress.py --known-list                       # triaged signatures
./qssm_stress.py --known-add tmp/results/0003-menu --known-note "why"
./rcon_probe.py --map start --runs 3
# If the loose assets are not in the standard location:
./rcon_probe.py --paks /path/to/id1/paks --loose-root /path/to/id1/paktest
```

`rcon_probe.py` is the focused external-control pass for state-dependent crash
bugs.  It starts a local listen server in the same throwaway sandbox, then
uses real localhost RCON packets after boot and across early signon, spawned,
dead, menu, save/load, changelevel, and demo-playback states.  It
repeats `viewpos`, `setpos`, `entities`, `edictcount`, `edicts`, demo seeking,
and render/cvar combinations at fixed and randomized positions.  Its output
goes under `tmp/rcon-probe/`; it shares the main harness' crash-report parsing,
liveness checks, journals, and finding format.  It never targets a public
server.

### Recommended local targets

Keep these content targets in the regular stress rotation when their local installs are available:

- **Arcane Dimensions `ad_tears`** — a good stress-test map for renderer, map, visibility, lightmap, and entity-transition coverage.
- **Immortal Lock (`immortal`)** — a strong mod target for gameplay/QC, asset, input/menu, save/load, and map-transition stress.

These are local test-content recommendations, not public-server targets. Run them through the corresponding local mod/game-root setup and keep the harness pointed at the local PAKs or loose files.

Findings land in `tmp/results/<run>/`:

| file | contents |
| --- | --- |
| `FINDING.txt` | classification, signature, symbolicated top frames |
| `repro.jsonl` | canonical ordered multi-actor event log, replayable with `--replay` |
| `repro.txt` | legacy command journal retained for compatibility |
| `inputs/` | fuzzed files plus content-addressed `inputs/sha256/<digest>` snapshots |
| `console.log` | full engine stdout for the run |
| `QSS-M-*.ips` | the macOS crash report, when there was one |
| `hang-sample.txt` | `sample(1)` stack trace, when the engine wedged |

Runs are offline by default: the web-download mirrors are blanked, because a
corrupted demo's precache list will otherwise send real HTTP requests for
attacker-named files. Pass `--allow-net` to exercise those paths deliberately.
Local ports and RCON passwords are randomized per campaign by default; use
`--fixed-local` only when an external local test needs a stable endpoint.

`tmp/results/REPORT.md` summarizes the campaign.  Duplicate signatures are counted,
not re-reported.

### Stress levels

`--stress-level` is semantic depth, not just an iteration multiplier:

| level | schedule |
| --- | --- |
| `L0` | self-test: valid control input and target reachability |
| `L1` | valid corpus: deterministic legal inputs and baseline stability |
| `L2` | boundary: field limits, truncation, and exact file/message ends |
| `L3` | lifecycle: reconnect, teardown, save/load, and map transitions |
| `L4` | hostile structured: format-aware malformed inputs and strict oracles |
| `L5` | soak/matrix: repeated lanes, multi-actor replay, and resource slope |

`smoke`, `standard`, `deep`, and `soak` remain compatibility aliases for `L0`,
`L1`, `L4`, and `L5`. Levels that exercise normal gameplay/audio enable sound;
use `--nosound` when you need the older fast/no-device behavior.

Use `--iterations` to override a level's per-scenario depth, and `--runs` or
`--minutes` to override its campaign budget.  A named level makes progression
repeatable and ensures higher levels visit every selected lane instead of relying
on random selection.

For independent throughput, use `--jobs N`, for example:

```bash
./qssm_stress.py --jobs 5 --stress-level deep --minutes 20 --nosound
```

This launches isolated campaign workers with separate result trees, temporary
homes, port ranges, seeds, and corpus staging directories.  Worker artifacts are
kept under a timestamped `parallel-*` directory and novelty inputs are merged
into the selected corpus after the workers finish.  It is intentionally opt-in:
five full client sessions can contend for GPU/window resources. `--jobs` is not
supported for replay, minimization, regression, or remote campaigns.

The port separation is important for dedicated helper engines: networking is
initialized before `autoexec.cfg` runs, so the launcher must provide `-port`
early rather than relying on the config file alone. Busy-port retries are
worker-local and are reported in the worker event log.

`lanes.json` is the machine-readable lane manifest. It records required hooks,
corpus adapters, supported levels, oracle contracts, and a minimum acceptance
test. Planned lanes are listed there explicitly so a missing hook is visible as
“planned” rather than silently counted as coverage.

The current `wirefuzz` and `servercmdfuzz` lanes are marked
`implemented_legacy_boundary`: they use the existing live `_stress_inject`
transport. The `msgboundary` lane now exercises the exact
`_stress_parse_servermsg`/`_stress_parse_clientmsg` entry points in a
stress-enabled Debug build, with current-protocol result telemetry. Raw client
datagram replay is now available through the stress-only
`_stress_replay_datagram` command. It feeds the server's real connectionless
and netchan receive functions using the active loopback client context; the
input/oracle contract and implementation status are recorded in
`parser_contracts.json`.

`protocolmatrix` makes protocol negotiation a first-class stress dimension. It
boots a fresh local dedicated server for NetQuake 15, FitzQuake 666, RMQ 999,
and BJP3 10002, then requires the real client to complete signon before the
actor pair is shut down. A higher stress level therefore adds protocol-state
coverage instead of only increasing random iteration counts.

When the parser/network seams are enabled, stress builds emit one-time
`STRESS_COVERAGE` records for server-message opcodes, client-message opcodes,
protocol profiles, and raw datagram outcomes. Inputs that discover a new
campaign-wide ID are saved as content-addressed `.bin` files under the
selected corpus directory (by default, `results/corpus`); later wire lanes
mutate those saved inputs. Each normal campaign deduplicates representatives
by coverage ID and moves redundant entries to `corpus/quarantine/`. Use
`--corpus-regress` to replay every remaining representative and write
`CORPUS_REGRESSION.md`.

`--regress <dir>` turns saved `repro.jsonl`/`repro.txt` files into a repeatable
suite.  The complete multi-actor event log is preferred; otherwise a minimized
legacy journal is used before `repro.txt`.  It returns success only when every
saved oracle reproduces, and writes
`REGRESSION.md` under the selected results directory.

Findings from multi-process scenarios also include `repro.jsonl`, the canonical
ordered event log.  It records actor starts, client/server commands, RCON
commands, barriers, and input installations.  Replay and regression prefer this
file, so `wirefuzz`, `servercmdfuzz`, and `clientserver` retain both sides of
the reproduction; `repro.txt` remains as a legacy client/server journal.

### Containment

The harness always applies per-engine CPU, address-space, file-size, and open-file
limits.  On macOS, add `--contain` for Seatbelt containment: only the run
directory is writable and network access is limited to IPv4/IPv6 loopback.  This
also denies process fork/exec and external URL/file-opening paths.  It is opt-in
because some SDL/GL or instrumented builds need additional macOS permissions.

```bash
./qssm_stress.py --contain --scenario wirefuzz --iterations 8
```

Use `--cpu-limit`, `--memory-limit-mb`, `--file-limit-mb`, `--fd-limit`, or
`--process-limit` to tune the limits for sanitizer or unusually large mods.

### Sanitizer builds

For a slower memory-safety pass, build the macOS target with sanitizers and
point the harness at the resulting executable:

```bash
xcodebuild -project macOS/QuakeSpasm.xcodeproj -target QSS-M -configuration Debug \
  CLANG_ENABLE_ADDRESS_SANITIZER=YES \
  CLANG_ENABLE_UNDEFINED_BEHAVIOR_SANITIZER=YES build
./qssm_stress.py --bin /path/to/sanitized/QSS-M \
  --scenario wirefuzz --iterations 4 --hang-timeout 60
```

Sanitizer startup and map loads are slower, so use a longer hang timeout and
fewer iterations while validating the build.  The oracles pick up ASan/UBSan
diagnostics from the log automatically, so an instrumented build finds memory
errors that a release build silently survives.

## How it drives the engine

Two channels:

* **the stress script** -- an append-only command file the engine polls once per
  frame (`Host_StressPoll`).  Append-only means commands never re-run, which is
  the trap with `exec`ing a rewritten cfg from an alias loop.  It works in every
  client state, including disconnected, in-menu, and demo playback, where rcon
  has no listen server to reach.
* **rcon over UDP** -- only usable while a server is up, kept because it
  exercises the real network command path.

`_stress_status` is both the state oracle and the liveness probe: it prints one
parseable line with `cls.state`, `cls.signon`, `sv.active`, `key_dest`,
`m_state`, the map name and the frame counter.

Menus are key-driven, so `_stress_key <keynum> [down|up|press]` and
`_stress_char <ascii|text>` inject input directly -- macOS blocks synthetic
keystrokes via `osascript` without accessibility permission.

## Scenarios

| name | what it stresses |
| --- | --- |
| `smoke` | boot, load, play, quit -- a fast sanity check |
| `menu` | every menu, randomized keys and text, the search palette, menu switches mid-edit |
| `mapchurn` | level loads, changelevel, *interrupted* loads, save/load, disconnects |
| `gameplay` | weapons, movement, cheats, teleports, HUD and view cvar churn |
| `demo` | record/replay/seek/timedemo, plus truncated and byte-flipped demos |
| `cmdfuzz` | every console command with fuzzed args, in four client states |
| `cvarfuzz` | extreme cvar values, then forced renders and level loads |
| `netchurn` | listen server, rcon command path, maxplayers/connect churn |
| `clientserver` | a client talking to a separate dedicated server over real UDP |
| `wirefuzz` | a **hostile server**: crafted `svc_*` payloads injected into a real connected client's stream |
| `servercmdfuzz` | the server-only command surface, delivered as real `svc_stufftext` |
| `clientwirefuzz` | raw client-shaped UDP frames replayed at the server receive boundary |
| `msgboundary` | the exact client and server message-parser entry points |
| `protocolmatrix` | a complete local signon under each supported server protocol |
| `filefuzz` | malformed maps, models, sounds and lightmaps through the real loaders |
| `md3fuzz` | malformed MD3 variants, with a valid control that must still behave |
| `corpus` | one saved novelty input replayed through its original boundary |
| `remote` | benign connect/play/disconnect against a real public server (`--remote host:port`); coverage only, never fuzzed |
| `chaos` | all of the above, interleaved |

`lanes.json` is the manifest behind this table, and the two are kept honest by
an assertion at import: a lane marked `implemented*` must have a scenario, and a
scenario must have an `implemented*` lane.  Lanes marked `planned` -- currently
`audio`, `netimpair`, `qcfuzz`, `formatfuzz`, `renderlife`, `inputctrl` -- are
declared but not built, and `--list` says so rather than letting the manifest
imply coverage that does not exist.

### The hostile network lane (`wirefuzz`)

Everything else drives `src_command`; `wirefuzz` goes the other way. The
dedicated server writes attacker-shaped bytes straight into a connected client's
`message`/`datagram` (via the `-stress`-only `_stress_inject` command), so they
reach the victim's `CL_ParseServerMessage` exactly as a malicious server's
would, with netchan sequencing intact. The corpus is structured, not random:
truncated records (the highest-yield family, since `MSG_ReadFloat`/`ReadDouble`
lack the bounds guard the integer readers have), byte mutations, boundary
precache/stat counts, `stufftext` payloads, `svc_download`/`svc_fog` opcode
confusion, and multi-record concatenations. Expected behaviour is graceful
disconnect, `Host_Error`, or an ignored packet; a crash, hang, filesystem
escape, or parser-invariant violation is a finding.

`remote` is deliberately benign — it only connects, plays and disconnects, as a
coverage lane and a source of real signon captures. **Never** point injection or
fuzzing at a third-party server.

### Acked journal / barriers

Commands are sent with an `@seq N` and can be gated with
`@barrier <field><op><value>` (e.g. `@barrier signon=4`, `@barrier frame>=900`).
The engine drains the queue only past a satisfied barrier, and `_stress_status`
reports `acked=`/`exec=`, so "the command buffer is starved or barriered" is
distinguishable from "the frame loop is wedged" — the hang oracle prints the
last acked/exec sequence when it fires.

## Oracles

1. process death by signal
2. a fresh `~/Library/Logs/DiagnosticReports/QSS-M-*.ips`
3. `Sys_Error` / `Host_Error` / unhandled or oversized `SZ_GetSpace` failures /
   hunk exhaustion / sanitizer output in the console log
4. loss of liveness -- no `_stress_status` reply within the hang timeout
5. **parser invariant** -- `STRESS_PARSE overread|short`, emitted when the client
   did not consume exactly `net_message.cursize` (an unguarded reader walked off
   the end, or a dispatch arm mis-sized a record)
6. **filesystem escape** -- any file written outside the allowed run paths after a run

The liveness oracle retries before crying hang, and records whether the process
was spinning or idle: macOS App Nap will throttle a backgrounded window hard
enough to look exactly like a wedged frame loop.  Runs pass
`-NSAppSleepDisabled YES` to suppress that.

A bare `SZ_GetSpace: overflow` message is the engine's handled
`allowoverflow` recovery path and is not a finding by itself. Likewise, a
`SCR_ModalMessage` stack is first treated as a modal-input recovery case: the
harness uses the priority `<script>.answer` channel, which the stress-enabled
engine drains from inside the modal loop, to answer scripted dialogs before
classifying the process as hung.

## Known issues

`known.json` holds triaged signatures so a campaign reports only what is new.
Two states, and they are opposites:

* **`open`** -- a real bug that is still open.  It is expected to keep
  reproducing: it lands in a separate "Known issues seen again" section of
  `REPORT.md`, does not count toward the unique-finding total, and does not
  fail the run.  A nightly campaign therefore stays quiet until something
  genuinely new turns up.
* **`fixed`** -- a bug that was fixed.  Reproducing it again is a regression,
  reported loudly with the signature prefixed `REGRESSION [id]` and failing the
  run.

`--regress` reads the same list.  A repro corpus is not a pass/fail suite --
`PASS` there means "the bug still reproduces" -- so the list is what makes the
output readable: a known-open issue that still reproduces is expected, a
known-fixed one that comes back is a failure, and a known-open one that has
*stopped* reproducing is reported as stale, with a pointer to close it out.

Add entries from real findings rather than writing signatures by hand:

```bash
./qssm_stress.py --known-add tmp/results/0007-filefuzz --known-note "negative BSP lump offset"
./qssm_stress.py --known-add tmp/results/0007-filefuzz --known-state fixed
./qssm_stress.py --known-list
```

Entries match on an exact `signature` or a `signature_re`, optionally narrowed
by `kind` and `scenarios`.  `--no-known` disables suppression entirely, and
`--known PATH` points at a different list.
