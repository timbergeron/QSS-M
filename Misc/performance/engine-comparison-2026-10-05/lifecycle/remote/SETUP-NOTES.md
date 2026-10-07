# Untimed setup required before Denver measurements

Target: `denver.quakeone.com:26000` (NetQuake). Use explicit `connectnq`
in FTE; ezQuake requires a separate QuakeWorld server.

The user reported recurring Windows Firewall approval dialogs and an FTE
update-source prompt. An untimed startup on 2026-10-05 confirmed the FTE
in-engine **Enable update source** dialog. Connection attempts collected
before clearing these prompts are diagnostic pilots, not benchmark samples.
All 15 local FTE sessions were recollected with native `-noupdates`; their
earlier attempts are retained as excluded evidence.

Run `preflight.py ENGINE` to open a stable benchmark executable path without
timing or automatic quit. The user handles security/permission dialogs.
Keep executable paths stable after approval, and preflight every engine's
connection before collecting five clean rounds. Reject any attempt requiring
manual interaction. Do not change engine releases during setup.

The Denver harness leaves the main command buffer empty after `connect` so
server-stuffed handshake commands are not delayed behind benchmark waits.
It stops only the owned client after native signon stage 4 or a 35-second
launch deadline. Cleanup termination is not a quit benchmark; the separate
local lifecycle sessions measure native quit normally.

Denver's map can rotate. Capture native server status before and after each
attempt and preserve the actual map and player count. Preinstall identical
map/resource bytes in all engine copies and collect within one map rotation;
if it rotates, retain affected attempts separately and repeat them on aerowalk.
The user authorized `cmd dm normal aerowalk` for restoring the selected map.
The reproduction harness withholds this command until the setup client has
completed signon and a final server status check reports exactly one player
(the setup client); it aborts if another player joins. This last-moment guard
is best effort because the public server has no atomic check-and-change API.

FTE and QSS-M use native post-signon quit callbacks. Other clients are stopped
after the timing endpoint; cleanup sends only NetQuake `clc_disconnect` from
exclusively reclaimed UDP source ports whose ownership was verified against
the engine PID before termination. Map restoration and cleanup are untimed.
