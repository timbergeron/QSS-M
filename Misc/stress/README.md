# QSS-M stress tester / crash finder

`qssm_stress.py` drives a real QSS-M build through randomized sessions and
reports anything that crashes, wedges, or dies with a fatal engine error.  It
covers the things a player actually does -- connecting to a server, changing
maps, playing, walking the menus, replaying demos -- plus argument fuzzing over
every registered console command and cvar.

## Requirements

* a QSS-M build containing the `-stress` hooks (`Host_StressInit` in `host.c`)
* `pak0.pak` (+ `pak1.pak` for the registered maps); found automatically in
  `~/Desktop/qssm/id1/paks`, or pass `--paks <dir>`
* loose base assets such as `gfx.wad`; the harness auto-detects
  `~/Desktop/qssm/id1/paktest`, or pass `--loose-root <game-root>`

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

### macOS quick start

Build a stress-enabled Debug app, then point the harness at the executable
inside the app bundle. Do not point `--bin` at an Xcode intermediate binary: it
does not carry the app's embedded SDL2 framework and will fail at boot with a
`dyld` `Library not loaded: @rpath/SDL2.framework` error.

From the repository root:

```bash
xcodebuild -project macOS/QuakeSpasm.xcodeproj -target QSS-M \
  -configuration Debug CODE_SIGNING_ALLOWED=NO build

export QSSM_BIN="$PWD/macOS/build/Debug/QSS-M.app/Contents/MacOS/QSS-M"
export QSSM_PAKS="$HOME/Desktop/qssm/id1/paks"

python3 Misc/stress/qssm_stress.py --bin "$QSSM_BIN" --paks "$QSSM_PAKS" \
  --stress-level deep --minutes 5 --nosound
```

For five isolated workers:

```bash
python3 Misc/stress/qssm_stress.py --bin "$QSSM_BIN" --paks "$QSSM_PAKS" \
  --jobs 5 --stress-level deep --minutes 5 --nosound \
  --results Misc/stress/tmp/results-5x5
```

The normal app bundle can also be used if it is already built and signed. The
`CODE_SIGNING_ALLOWED=NO` form is useful for local stress builds when the
optional Dock Tile plugin prevents the final ad-hoc signing step; the app
executable and embedded SDL2 framework are still produced for the harness.

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

Stress-enabled Debug builds also emit one-time `STRESS_COVERAGE` records for
server-message opcodes, client-message opcodes, protocol profiles, and raw
datagram outcomes. Inputs that discover a new campaign-wide ID are saved as
content-addressed `.bin` files under the selected corpus directory (by
default, `results/corpus`); later wire lanes mutate those saved inputs.
Each normal campaign deduplicates representatives by coverage ID and moves
redundant entries to `corpus/quarantine/`. Use `--corpus-regress` to replay
every remaining representative and write `CORPUS_REGRESSION.md`.

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
harness uses the priority `<script>.answer` channel to answer scripted dialogs
before classifying the process as hung.

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
