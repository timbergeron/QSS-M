# Dedicated server optimization verification

Baseline: `e704fc911e64f4fb1cafc0df8992e8f363db4543`. Changes are on `codex/dedicated-optimization`.

## Changes

- Dedicated waiting sleeps most of the remaining interval at once, reserving 1 ms for scheduling variance. `SDL_DelayPrecise` is used only for the final <=2 ms, avoiding its repeated 1 ms sleeps over the whole interval. Quit checks remain between chunks of at most 50 ms. `sv_idlesleep` has been removed; old configs containing it will report an unknown command. Empty and populated servers use the same deadline wait; the idle-only cap and client scan are gone. The short precise tail can busy-wait, trading some CPU time for lower scheduling overshoot; it is not a purely blocking sleep.
- Each client slot owns its fat-PVS cache. The public renderer/QC cache remains separate. All use model pointer, model generation and nearby leaf membership for validity; oversized leaf keys retain the uncached fallback. Skyroom merging uses separate snapshot scratch. Both replacement-delta and legacy snapshot writers use the client cache. Allocations retain their per-slot high-water capacity across connections/maps, avoiding leaks from client structure resets.
- `NET_SendToAll` still pumps transport ACKs/retransmits and retains its delivery timeout. Pending iterations sleep at most 1 ms and no longer wait on dropped, replaced, disconnected or failed sockets. The return value counts undelivered messages, including clients lost during the wait; shutdown reports them as undelivered. The receive pump can run disconnect QC; it does not parse gameplay messages.
- Dedicated host frames skip graphics, particles, HUD preload, audio, dynamic-light decay and platform/presence updates. Shared console, commands, deferred callbacks, networking, URI and download work remains.

## Initial local benchmark (before idle-cap review fix)

macOS arm64 Release, `immortal` BSP2 with its installed Quoth assets, 16 stationary synthetic UDP clients negotiating replacement deltas. All 16 reached full signon. Temporary identical instrumentation timed Host_Frame and SV_SendClientMessages; the final patch contains no profiling hooks. These measurements predate removal of the idle cap. Each populated result uses the last 180 fully spawned samples. Process CPU was sampled over ten seconds.

| Measurement | Before | After |
| --- | ---: | ---: |
| Populated tick interval, median | 50.588 ms | 50.031 ms |
| Populated tick interval, p95 | 51.114 ms | 50.184 ms |
| Host work, median | 8.116 ms | 7.396 ms |
| Sending updates, median | 1.924 ms | 1.013 ms |
| Sending updates, p95 | 2.330 ms | 1.236 ms |
| Process CPU, populated | 14.9% | 13.5% |
| Empty tick interval, p95 | 57.999 ms | 50.146 ms |
| Process CPU, empty | 10.6% | 12.0% |

This is a local stress comparison, not a representative player benchmark. The synthetic clients send keepalives but no movement or delta-frame acknowledgements; the map continues running game logic. Logs include packet-overflow notices in both versions. Random game activity and the short sampling period affect total-frame/CPU comparisons. The results support the cache and cadence improvements; they do not establish an empty-server CPU improvement or a universal speedup. The later review confirmed that the remaining idle cap unnecessarily increased empty-server wakeups; the CPU increase must not simply be dismissed as noise. A bulk-only timer candidate had a 51.028 ms median period; the short precise tail corrected the observed scheduling tradeoff.

Measured aggregate alias loading was 36.1 ms before changes (28.8–32.6 ms on later warm runs), below the proposed ~50 ms prioritization threshold even if all alias work could be eliminated. Model loaders remain unchanged. This is alias-loading time, not total map-change time. Shared entity-state preparation also remains deferred.

## Review follow-up

- A production-loop regression reproduced 32 sleep calls over four empty ticks versus eight over four populated ticks with the default idle cap. Removing the cap gives eight calls in both cases with identical modeled work. The regression retains the empty/populated comparison; checks of legacy idle values were removed with the obsolete cvar.
- After the idle-cap fix, the dedicated-wait, reliable-broadcast, randomized visibility-cache and client frame-pacing tests passed; universal Debug and arm64 Release builds succeeded. A fresh two-second-tick SIGTERM check exited in 73.6 ms. Empty-server CPU was not rebenchmarked, so the earlier CPU numbers are retained only as historical results.
- Kept the precise tail as the timing/CPU tradeoff described above. Its busy-wait duration varies with platform sleep overshoot; no fixed per-tick CPU cost is established here.
- The old loop did advance `net_time` through `NET_CanSendMessage` (and sends). The explicit `SetNetTime` before receiving ensures retransmit/timeout checks see current iteration time; it is not evidence that old timers were entirely frozen or the sole cause of five-second waits.
- Moved this report into the existing `Misc/performance` directory. Plan/spec scaffolding is preserved outside the repository at `/tmp/qssm-dedicated-impl/review-followup/superpowers/`.

## Verification

Passed:

- `python3 Misc/stress/test_dedicated_wait.py`: production loop with fake clock; deadlines, empty/connected, work overruns, short/long/nonpositive ticks, coarse scheduler slop and signal checks. Failed on the old idle overshoot before implementation; the coarse-slop check failed on bulk-only waiting before the precise-tail adjustment.
- `python3 Misc/stress/test_view_caches.py`: production PVS functions and randomized BSPs under ASan/UBSan; alternating clients/renderer, skyroom scratch, QC queries, slot reuse, generation/model changes, key overflow, and renderer leaf-cache behavior. Alternating-client check failed before implementation.
- `python3 Misc/stress/test_send_to_all.py`: production function under ASan/UBSan; delayed/missing ACKs, dead/dropped sockets, send errors, timeout bounds, empty and loopback cases. Busy-polling check failed before implementation.
- Existing frame-pacing, chunked-download and multicast-init checks.
- Universal macOS Debug build and arm64 Release build via `macOS/QuakeSpasm.xcodeproj`, target `QSS-M`. Existing compiler/deprecation/linker warnings remain.
- Live UDP map reload: deliberately dropped the first reconnect ACK from each of 16 clients; retransmission completed and all 16 signed on again. Full reload plus signon took 8.28 s; this does not isolate the reliable-send wait.
- Before cvar removal, Debug shutdown with `sys_ticrate 2` and `sv_idlesleep 0`: actual RCON status established readiness, then SIGTERM caused process exit in 69.8 ms (status 143).
- Final Debug listen-client smoke: `sv.active=1`, signon 4; both server/client visibility paths, screen updates and HUD preload ran.
- Final Debug dedicated frame probe: shared URI/download/deferred/network calls ran; screen/HUD work was skipped.
- Separate read-only code review: no important correctness findings. Corrected one misleading transport comment.

Limits:

- `test_tick_deadlines.py` fails an enum-to-int signedness warning treated as an error in unchanged Discord code. The same failure was reproduced in the unmodified primary checkout.
- `test_msgread_bounds.py` requires a stress-enabled binary; its hardcoded lookup selected a different non-stress binary, so it could not run with this ordinary build.
- Live ICE/browser reconnect, Windows/Linux runtime and pixel-for-pixel listen-client parity were not tested. Source review confirmed the existing ICE receive pump continues to run ProcessModule and reliable retransmits.

Local evidence and temporary benchmark drivers: `/tmp/qssm-dedicated-impl/` (`before/results.json`, `after-tail/results.json`, build logs and runtime probe JSON/logs). All engine test processes were stopped; only scratch configurations were used.
