# Six-engine lifecycle benchmark expansion

> Execute inline using the existing benchmark infrastructure.

**Goal:** Complete startup, quit, launch-to-map, first-map, subsequent-map, and local connection measurements for all six engines, retaining the existing nine-map FPS results.

**Architecture:** A portable Python harness copies released binaries and identical stock Quake assets into isolated directories. Native console markers and signon events provide high-resolution timing endpoints. A self-contained HTML report exposes medians, ranges, individual samples, protocols, and downloadable raw evidence.

**Constraints:** Five measured samples per engine/test; 800 × 600 window; VSync off; lifecycle cap 144 FPS; fresh processes for launch and connection; warm OS caches; no changes to installed game settings. Use NetQuake signon stage 4 for compatible clients and ezQuake's native f_spawn trigger after CL_MakeActive for QuakeWorld. Label protocol/server differences and missing endpoints explicitly.

- [x] Pilot the portable lifecycle harness and compatible local servers; verify native events and clean exits.
- [x] Collect five rounds across all six engines, rotating order; preserve commands, configurations, hashes, logs, and observed events.
- [x] Generalize lifecycle charts, samples, summaries, and exports while preserving all existing FPS measurements.
- [x] Independently verify timing arithmetic against events, all expected samples, exports, desktop/mobile controls, and absence of browser errors.

Denver follow-up completed: 25 connections on aerowalk at explicit port 26000,
with identical map/custom-sound bytes and an empty-server check before each
attempt. Rotated-map attempts are excluded. ezQuake remains inapplicable to
this NetQuake endpoint and has its separate local QuakeWorld timing. All 15
local FTE sessions were replaced using native -noupdates after its startup
update-source prompt was confirmed. Final verification reconstructs 205
positive timings from 115 native logs, verifies 26 excluded attempts, exact
exports, mobile layout and all nine FPS maps. The 324 FPS passes are preserved.
