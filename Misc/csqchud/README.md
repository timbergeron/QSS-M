# csqchud: a movable status bar in portable CSQC

A sample HUD that recreates QSS-M's three status bar styles (`scr_sbar 1`
classic, `2` QuakeWorld, `3` Quake re-release) using only standard CSQC
builtins. Every piece can then be moved, scaled or hidden.

It only defines `CSQC_DrawHud`, so it also works as **simple CSQC**: a
`csprogs.dat` the player drops into their own gamedir, on any server. It uses
standard CSQC builtins, plus optional QSS-M `serverkey()` keys (see
[below](#engine-data-through-serverkey)) that read as empty on other engines.
The same progs runs unchanged on FTEQW; only the widgets fed by those keys
stay blank there.

At the default layout the sample matches the engine's own status bar pixel for
pixel in all three presets (see [Checking it](#checking-it)).

## Using it

1. Build it (below) and copy `csprogs.dat` into a gamedir.
2. Set `scr_csqc_cleanhud 1` so QSS-M stops drawing its own gameplay overlays
   (match clock, speed, ping and so on) over a custom HUD. Use `2` to hide the
   crosshair and the demo bar as well.

## Layout cvars

`chud_preset` picks the starting layout: `0` follows `scr_sbar`, `1`–`3` force one.

Each widget then has four cvars. An empty value means "use the preset's value",
so a preset stays exact until you override something.

| cvar | meaning |
|---|---|
| `chud_<w>_show` | `0` hide, `1` show |
| `chud_<w>_anchor` | `"ax ay"`: the point of the widget pinned to the same point of the screen. `"0 0"` top-left, `"0.5 1"` bottom-centre, `"1 1"` bottom-right |
| `chud_<w>_ofs` | `"x y"`: offset from that point, in widget units |
| `chud_<w>_scale` | multiplier on top of `scr_sbarscale` |

Widgets (`<w>`):

| widget | contents | presets |
|---|---|---|
| `status` | sbar row: armor, face, health, current ammo | 1, 2 |
| `ibar` | classic inventory strip: weapons and ammo counts | 1 |
| `items` | keys, powerups, sigils | 1, 2 |
| `frags` | top frag counts (`chud_frags_count`, default 2, like QSS-M), or team totals and the match clock on ProQuake/CRMod servers | 1 (multiplayer) |
| `qwammo` | side column: weapons and ammo counts | 2, 3 |
| `qe_armor`, `qe_health`, `qe_ammo`, `qe_keys` | re-release corner blocks | 3 |
| `speed` | speed readout, white / red over 400 / gold over 600 | follows `scr_showspeed` |
| `movekeys` | movement keys | follows `scr_movekeys` |
| `loc` | location name, centred under the crosshair | follows `r_showlocs` |

`speed`, `movekeys` and `loc` follow the engine's own toggles and default to
the engine's positions. With `scr_csqc_cleanhud 1` the engine stops drawing
them, and the HUD's copies can go anywhere. The speedometer bar
(`scr_showspeed 2`) isn't reproduced, only the number. The engine draws the
movement arrows by rotating `^`; `drawcharacter` can't rotate, so the sample
uses `^ v < >`.

Examples:

```
chud_status_anchor "0.5 0"; chud_status_ofs "0 8"     // status row at the top
chud_qwammo_anchor "0 0.5"; chud_qwammo_ofs "4 0"      // ammo column on the left
chud_items_show 0                                      // no items row
```

Set values that contain spaces in the console or a cfg. On QSS-M's command
line the quotes are lost, so `+set chud_status_ofs "0 8"` arrives as two
arguments and the `set` is refused. Also keep the command line under 50
arguments: Quake drops everything past that, `+map` included.

The sample also respects `viewsize` (110 and 120 hide parts, as the engine
does), `scr_sbaralpha`, `scr_sbarshowqeammo` and `scr_sbarscale`. On engines
without those cvars it falls back to their QSS-M defaults.

## Building

`src/qscsextensions.qc` comes from the engine (not committed). Put
`pr_dumpplatform -Tcs -O qscsextensions` in a cfg, `+exec` it (the engine's
argv parser eats `-T...` on the command line), and copy the result into `src/`.

```bash
cd Misc/csqchud/src && ./build.sh /path/to/fteqcc
```

This writes `csprogs.dat` (the sample) and `ref.dat` (reference progs used only
by `compare.py`).

## Checking it

```bash
python3 Misc/csqchud/compare.py --bin <QSS-M binary> --basedir <throwaway basedir>
```

The basedir needs `id1/pak0.pak`, `id1/pak1.pak` and `qssm.pak` at its root.
For each preset the script runs `start` twice at 800x600. Each run gives
everything with `impulse 9` and `give a 200`, waits out the pickup flash, then
takes a screenshot:

- `chudref`: `ref.dat`, a full CSQC that has the engine draw its own status bar
  through `VF_DRAWENGINESBAR`
- `chud`: the sample

It then diffs the HUD rectangles. The last run: 0 differing pixels in all nine
rectangles (classic bar, QW status/items/column, re-release
armor/health/ammo/keys/column).

`--hud-arg` passes extra console arguments to the sample run only, which is
handy for a layout cfg placed in `<basedir>/chud/`: `--hud-arg=+exec
--hud-arg=mylayout.cfg`. Any moved widget makes its rectangle fail. Moving one
widget by a single pixel gave 5102 differing pixels, which shows the check
catches real differences.

## Engine data through serverkey()

Match timers and team scores reach the client as binary ProQuake messages,
which CSQC never sees. QSS-M exposes them through the standard `serverkey()`
builtin as read-only `pq_*` pseudo-keys. Nothing new is registered, and other
engines just return `""`, so the sample falls back to plain player frags there.

| key | value |
|---|---|
| `pq_clock` | seconds shown by the classic bar's match clock (`""` when there is none) |
| `pq_clockmode` | `up`, `down`, `prematch` or `suddendeath` |
| `pq_clockwarn` | `1` when the engine draws it red (final minute, sudden death) |
| `pq_matchpaused` | `1` while a match is paused |
| `pq_teamgame` | `1` when the server sends team scores |
| `pq_teams` | space-separated team colours (0-13) that have a score |
| `pq_teamfragsN` | frags of team colour N |

Values QSS-M's own overlays already show on screen. These are display values
only; raw velocity and position stay withheld from simple CSQC, as in FTE:

| key | value |
|---|---|
| `qssm_speed` | units/sec the speedometer shows (spectators: the tracked player) |
| `qssm_loc` | location name for the view, as team chat's `%l` reports it |
| `qssm_movekeys` | keys `scr_movekeys` lights: 1 forward, 2 back, 4 left, 8 right, 16 jump, 32 swim up |

The clock comes from the same function the engine's status bar uses
(`Sbar_GetMatchClock`), so the two always agree, including the player's
`scr_matchclock_countup` preference. On a live CRMod 7 server the sample's
frag-slot clock read the same value as the engine's `scr_matchclock` in the
same frame. Speed, movement keys and loc name matched the engine's overlays in
the same frame in single player, and drew correctly on the live server with
`scr_csqc_cleanhud 1`.

## What it doesn't reproduce

- demo percentage and frame counts; the engine's demo bar covers these, and
  `scr_csqc_cleanhud 1` keeps it
- `scr_sbarfacecolor` team face border: doable with
  `getplayerkeyvalue(player_localnum, "bottomcolor_rgb")`
- the record indicator
- the mini deathmatch overlay
- powerup timer bars and combined-powerup faces (QSS-M-only pics)
- hipnotic/rogue weapons and items

Approximations:

- **Pain face:** QSS-M starts it on `svc_damage`, which simple CSQC never sees,
  so the sample starts it when health drops.
- **Tab scoreboard:** draws the scorebar with kills and secrets. It isn't
  QSS-M's map/server/session-time line.

## Portability notes

- FTE leaves `time` frozen inside `CSQC_DrawHud`. The sample times pickup
  flashes, the pain face and its autotest from `cltime`, which both engines
  update.
- `getplayerkeyvalue` with a negative player number reads the frag-sorted list
  on both engines.
- FTE ignored the autotest's `impulse 9` and `give`, so FTE runs show a bare
  inventory. Layout-wise the HUD draws the same there.
