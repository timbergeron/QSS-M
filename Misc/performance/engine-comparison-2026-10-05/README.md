# Quake Engine Lab benchmarks

Start with **KNOWHOW.md** (pitfalls, fairness rules, findings). The scripts that produced every dataset are in **harness/**.

**index.html** (rebuilt 2026-10-06) is the current report: standings, lifecycle, map loading, nine-map FPS, the QSS-M FPS investigation, and method/data. All six engines were re-measured on 2026-10-06 (NVIDIA driver 610.88) with the current QSS-M source; the installed QSS-M 1.6.9 build was re-measured the same day for the "before" markers. Raw data, logs, the rerun driver (refresh.py) and the page generator (build_report.py + template.html) are in **data-2026-10-06/**. One capped cell (Ironwail on Aerowalk, first four passes at 60 FPS) was excluded and re-measured; the capped passes remain under `excluded_samples`. The Denver connection test was not repeated and is shown as October 5 data. The previous page is kept as **index-2026-10-05.html**; the sections below describe the October 5 datasets.

# Six-engine lifecycle and FPS benchmarks

Open index.html directly: the dashboard and raw JSON exports work offline. Startup, quit, first/subsequent map loads, launch-to-map, and local joining now cover QSS-M, FTEQW, ezQuake, Ironwail, vkQuake, and the requested March 1 2024 QSS archive. Five rounds produce 90 local sessions plus 25 Denver connection sessions, with 205 positive timings. Denver uses aerowalk at explicit port 26000, with identical cached map/sounds and an empty server before every attempt. ezQuake is inapplicable to this NetQuake endpoint. FTE local sessions were repeated with native -noupdates after confirming its startup update-source prompt; prior FTE attempts are excluded. ezQuake uses a separate QuakeWorld server and is not ranked against NetQuake connection times.

results.json and samples.csv contain the final six-engine lifecycle run. lifecycle/logs contains the 90 local native logs; lifecycle/remote/logs contains the 25 Denver logs. Excluded FTE/ezQuake attempts and rotated-map Denver attempts are retained separately. fps-results.json, fps-samples.csv, and fps/logs preserve the existing 324 FPS passes, with 54 warm-ups and 270 measured passes. original-results.json and the old top-level logs preserve the superseded two-engine lifecycle baseline and are excluded from current summaries.

## Reproduce lifecycle measurements

Use Windows with Python 3. Supply licensed pak0.pak and pak1.pak under fps/assets/id1 (game data is intentionally omitted). Set the four released executable paths in fps/acquisition.json and the installed QSS-M/FTE paths in fps/measure.py. The lifecycle harness imports only native package preparation helpers from that module; it does not run FPS tests. Run from a preserved copy of this report directory to avoid replacing its measurements, then run python benchmark.py --pilot followed by python benchmark.py. Run on the normal Windows desktop; the sandbox's separate desktop can cause focus throttling or hidden dialogs. Ports 26001, 26002, and 27501 must be free.

Executables, DLLs, and game data are copied into isolated engine directories. Native configuration scripts are also stored in a small pak2.pak to make search precedence reliable. Only benchmark-owned processes are quit or terminated on failure. Dedicated servers bind loopback, remain private, and receive native authenticated quit packets. vkQuake's native preference-directory qconsole.log is read without editing user settings; unique session markers reject stale output. Source paths, executable/data hashes, commands, staged scripts, events, exit codes, and physical window sizes are retained in results.json. Read the HTML methodology before interpreting the results.

For Denver, approve permission dialogs during untimed preflight.py setup, then run remote.py. The user-authorized cmd dm normal aerowalk command restores the map only on an empty server. Native signon stage 4 ends timing. FTE and QSS-M disconnect through native post-signon callbacks; other owned clients are terminated after readiness, then the runner sends only clc_disconnect using exclusively reclaimed, verified client-owned UDP source ports. No public-session quit timing is measured. Server status/map/player count is checked around each attempt. Do not mix rotated-map attempts. Cached custom resource hashes and source-port ownership are retained in results.json.

build_report.py is the local generation script; it expects the sibling scratch FPS generator/template directory used for this run. For standalone reproduction, inspect raw logs/results directly or adapt its template paths. FPS reproduction is described in fps/measure.py and fps/record.py; supply the licensed assets and the retained shared demos.


## QSS-M live FPS diagnostic

The FPS section now includes supplemental same-process Aerowalk repeats, verified 4K checks, native cvar queries, and a comparison with the saved main config’s sky settings. See fps/diagnostics/results.json and its native logs. These 48 passes are excluded from the original 324-pass ranking. First-process timedemos do not predict warmed live-game FPS; scene-cache-disabled repeats retain the first-pass penalty. The exact internal warm-up cost has not been isolated.


## QSS-M optimization investigation

Supplemental warmed Aerowalk comparisons and frame-stage profiling are under fps/optimization. See comparison.json and video-profile.json for the exact methods, build identities, and limitations. Five engines have completed native-scheduled warm sequences; vkQuake has no complete native warm sequence and is excluded. Production source and installed binaries were unchanged. Profiler scripts are audit copies and reference the original scratch layout .codex-build/engine-fps-optimization-20261005. These exploratory results do not replace the original 324-pass nine-map ranking.


## Observer HUD cache source change

The subsequent approved source change is in Quake/gl_screen.c, with a behavioral regression test in Misc/stress/test_observer_hud_cache.py. It caches optional icons including misses until SCR_LoadPics on HUD/game reload. fps/optimization/hud-cache/results.json contains the same-compiler nine-map 800x600/4K A/B data: 360 native passes, 144 warm-ups and 216 measured passes, two blocks per build/resolution in ABBA order. GPU profiling data is separate. The installed Desktop/qssm binary and config were not edited. A portable candidate binary with matching DLLs is packaged separately; it is not an engine release. No statistical significance or new all-engine rank is asserted. Reproduction scripts are audit copies referencing the original scratch layout.


## Targeted renderer experiments

Diagnostic runs in fps/optimization/probes/ keep separate baselines and do not replace release rankings. Native logs verify every pass, physical window geometry, eight measured samples per mode/resolution, and discarded warmups. The targeted tests cover forced synchronization, hiding demo controls, queue flush placement, the offscreen gamma path, and a private shader-state cache if present. Separate disposable CPU timers isolate demo parsing/snapshot/rewind costs; overlapping intervals must not be added. Only the tested observer HUD cache change is in production source.


## Direct timedemo rendering profile

Separate disposable profiler runs in fps/optimization/probes/timedemo-render.json cover three maps at 800x600 and 4K: 48 passes, 12 warmups, 36 measured diagnostics. CPU 3D/HUD/presentation/outside-render stages and asynchronous GPU timestamp intervals overlap; query APIs add overhead and can shift CPU driver charges. Outside-render includes polling and other host work. Profiler frames include one extra rendered frame vs native timedemo FPS count. These instrumented FPS values are excluded from engine rankings and nine-map source A/B results. The tested small source/setting changes have not established a material FPS gain; the gap remains open.


## Fastest map loading (final source A/B, 2026-10-06)

The loading panel now reports the final uncommitted source build. Nine interleaved fresh-process runs per engine (one warmup each), same lifecycle script and signon-4 endpoint: initial load 226.5 -> 96.6 ms (Ironwail 0.8.2 107.5, vkQuake 1.36.0 147.0); map change 238.5 -> 42.3 ms (Ironwail 126.2, vkQuake 127.0). QSS, FTEQW and ezQuake were not rerun; their original medians are far slower.

Changes: load-scoped loose-directory cache; local server steps every frame and the FPS cap is lifted while a local client signs on; sounds kept across map changes with stale-entry recycling; texture and buffer names from bulk pools (avoids NVIDIA threaded-driver round trips); last frame held (max 2 s) during a local signon. Each step has its own A/B under loading-optimization/final-2026-10-06/steps.

Checks: timedemo FPS +2-4% vs the previous build (fps-check), quit unchanged (quit-ab), 1,700 texture mip uploads identical (texture-verification), six compiled regression tests pass. Synchronous GL debug output from startup was rejected: it cut first-load time but cost 5-9% FPS (rejected-startup-sync*). Raw logs, scripts, source.patch and a test build zip are in loading-optimization/final-2026-10-06.


## Gameplay demos and the FPS investigation (2026-10-06, later)

Two gameplay timedemos join the nine-map suite: a 16-player CTF match on ctf3m2 (ctfmatch.dem, CTF assets packed from the local install, QSS-M with scr_autoid 0) and Sphere's 2:14 ad_tears easy run from Speed Demos Archive (Arcane Dimensions 1.80p1, the version SDA lists; ezQuake cannot play it). Same fresh-process method; runner, logs, asset manifest and results are in data-2026-10-06/gameplay-demos/. Ironwail and vkQuake have no unfocused-sleep opt-out, so the runners bring each engine window to the foreground; Ironwail/vkQuake load only consecutive pakN files, so benchmark configs use the next free pak number.

The nine-map suite was re-run with a QSS-M build that loads demo precaches inside the serverinfo frame (Quake/cl_parse.c). Before, QSS-M did its demo map load inside the first timed frames; the fix raised fresh-process FPS 26-33%. Investigation data (per-pass probes, frame budget, present-floor test, HUD on/off) is in data-2026-10-06/gameplay-demos/investigation-20261006.json.
