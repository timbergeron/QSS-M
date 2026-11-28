# csqchud: a movable id1 status bar in CSQC

An id1 sample HUD tested on QSS-M and FTEQW, with optional QSS-M extensions.
It recreates QSS-M's three status bar styles (`scr_sbar 1` classic, `2`
QuakeWorld, `3` Quake re-release) and lets every piece be moved, scaled or
hidden.

It only defines `CSQC_DrawHud`, so it works as **simple CSQC**: a `csprogs.dat`
the player drops into their own gamedir, on any server. Standard CSQC paths
come first (FTE's `STAT_TIME`/`STAT_MATCHSTARTTIME` + `timelimit` clock and
`teamplay`-gated `team` playerkey summing); optional QSS-M extensions (see
[below](#optional-qss-m-extensions)) return `""` on other engines and degrade
gracefully.

At the default layout the sample matches the engine's own status bar pixel for
pixel in all three presets, for id1 and for the Hipnotic and Rogue layouts (see
[Checking it](#checking-it); the mission packs are checked with placeholder
art, see below). It has
not been runtime-tested on DarkPlaces (DarkPlaces accepts a `CSQC_DrawHud`-only
progs in source, but its playerkey implementation lacks the `team` and RGB
colour keys this sample reads).

Like the engine, it picks the Hipnotic or Rogue layout from which lumps
`gfx.wad` has (`inv_laser` / `r_invbar1`), so it switches automatically with
the game directory.

## Using it

1. Build it (below) and copy `csprogs.dat` into a gamedir.
2. Turn off only the engine overlays the HUD replaces, so they aren't drawn
   twice. Everything else (ping, observer scores, demo controls, FPS,
   crosshair) keeps working as usual:

   | HUD widget | turn on | engine overlay to turn off |
   |---|---|---|
   | `speed` | `chud_speed_show 1` | `scr_showspeed 0` |
   | `loc` | `chud_loc_show 1` | `r_showlocs 0` |
   | match clock in `frags` | (on with preset 1) | `scr_matchclock 0` if you don't want both |

   The HUD needs no engine setting for this. On FTEQW there are no such
   QSS-M overlays, so there's nothing to turn off.

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
| `frags` | top frag counts (`chud_frags_count`, default 2, like QSS-M), match clock, and team totals | 1 (multiplayer) |
| `qwammo` | side column: weapons and ammo counts | 2, 3 |
| `qe_armor`, `qe_health`, `qe_ammo`, `qe_keys` | re-release corner blocks | 3 |
| `speed` | speed readout, white / red over 400 / gold over 600 | follows `scr_showspeed` |
| `loc` | location name, centred under the crosshair | follows `r_showlocs` |

`speed` and `loc` default to following the engine's own toggles and positions.
Set `chud_speed_show`/`chud_loc_show` to make them independent; then turn the
engine's copy off and put the HUD's anywhere. The speedometer bar (`scr_showspeed 2`) isn't
reproduced, only the number. Movement keys (`scr_movekeys`) stay on the native engine overlay, which can be
positioned anywhere with
`scr_movekeys_x`, `scr_movekeys_y` (`-1` = preset default, `0`–`100`% across the
screen) and `scr_movekeys_scale`.

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

Engine settings (`viewsize`, `scr_sbar`, `scr_sbaralpha`, `scr_sbarshowqeammo`,
`scr_sbarscale`, `scr_showspeed`, `r_showlocs`, `r_showlocs_y`) are declared as
`autocvar_*` globals so they exist with sensible defaults on engines that don't
define them natively.

## Building

`src/qscsextensions.qc` comes from the engine (not committed). Put
`pr_dumpplatform -Tcs -O qscsextensions` in a cfg, `+exec` it (the engine's
argv parser eats `-T...` on the command line), and copy the result into `src/`.

```bash
cd Misc/csqchud/src && ./build.sh /path/to/fteqcc
```

This writes `csprogs.dat` (the sample) and `ref.dat` (reference progs used only
by `compare.py`). With `src/qsssextensions.qc` present (`pr_dumpplatform -Tss
-O qsssextensions`) it also builds `chudss.dat`, the tiny server progs the
mission-pack checks use.

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

It then diffs the HUD rectangles.

`--hud-arg` passes extra console arguments to the sample run only, which is
handy for a layout cfg placed in `<basedir>/chud/`: `--hud-arg=+exec
--hud-arg=mylayout.cfg`. Any moved widget makes its rectangle fail.

### Mission packs

```bash
python3 Misc/csqchud/compare.py --bin <QSS-M> --basedir <throwaway basedir> \
    --missionpacks <basedir>/id1/pak0.pak
```

Hipnotic and Rogue are commercial, so the check doesn't use their data.
`make_placeholder_wads.py` builds a `gfx.wad` holding id1's lumps plus every
lump name the engine looks for; each placeholder is an id1 pic of the same size
with its palette shifted, so a wrong lump in a slot shows up as a difference.
`chudss.dat` forces the item bits, active weapon, ammo and armour from cvars
every frame (bits 23-31 through `items2`). Five scenarios cover the laser
cannon, mjolnir and the grenade/proximity overlap, the wetsuit and empathy
shields, Hipnotic's keys on the status row, both Rogue inventory bars, every
powered weapon icon tested (lava nailgun, plasma gun), Rogue's armour and ammo
icons, the shield and antigrav. This proves positions and selection logic match
the engine, not the real art.

The same run also checks an id1 gamedir with a stray loose
`gfx/inv_laser.tga`: like the engine, the sample only detects a mission pack
from `gfx.wad`, and only when the pack's whole lump set is there.

## Standard match clock and team scores

- **Match clock:** QSS-M populates FTE's `STAT_TIME` (17) and
  `STAT_MATCHSTARTTIME` (18) in milliseconds from ProQuake timers whenever the
  server didn't send those stats itself and `serverinfo` has a positive
  `timelimit`. The sample reads `STAT_TIME`/`STAT_MATCHSTARTTIME` +
  `serverkeyfloat("timelimit")` first (the standard FTE convention).
- **Team totals (fallback):** when `pq_teams` is empty and
  `serverkeyfloat("teamplay") > 0`, the sample groups connected non-spectator
  players by `getplayerkeyvalue(i, "team")` and sums their frags, which works
  with QuakeWorld team names on FTEQW. Note one parity difference on CRMod FFA
  servers (where `pq_teams` is empty and `teamplay` is `0`): the sample shows
  player frags where the engine's classic bar shows none.

## Optional QSS-M extensions

Everything below uses existing builtins (`serverkey()` and
`getplayerkeyvalue()`) and returns `""` on engines that don't implement them.

### ProQuake/CRMod `serverkey()` keys

Match-global ProQuake/CRMod data that standard stats and playerkeys cannot
express (prematch countdown, pause, sudden death, count-up, timed overtime,
servers without `timelimit`, and server-authoritative team scores including
CRMod arena round wins):

| key | value |
|---|---|
| `pq_clock` | seconds shown by the classic bar's match clock (`""` when there is none) |
| `pq_clockmode` | `up`, `down`, `prematch` or `suddendeath` |
| `pq_clockwarn` | `1` when the engine draws it red (final minute, sudden death) |
| `pq_matchpaused` | `1` while a match is paused |
| `pq_teams` | space-separated ProQuake team colours (`0`–`13`) that have a score |
| `pq_teamfragsN` | frags/score of ProQuake team colour `N` |

### Per-player `getplayerkeyvalue()` keys

Display values already drawn by QSS-M's native overlays, exposed per player
slot so they fit splitscreen/MVD semantics (raw velocity and origin remain
withheld from simple CSQC):

| key | value |
|---|---|
| `speed` | units/sec for the viewed player (interpolated spectator `tinfo` speed when observing, else local velocity), or `tinfo` speed for other slots when available |
| `location` | map location name for the viewed player only (restricted to `cl.viewentity` so untrusted HUDs cannot use it as radar; `""` when no `.loc` is loaded) |

## What it doesn't reproduce

- demo percentage and frame counts; the engine's demo bar covers these
- `scr_sbarfacecolor` team face border: doable with
  `getplayerkeyvalue(player_localnum, "bottomcolor_rgb")`
- the record indicator
- the mini deathmatch overlay
- powerup timer bars and combined-powerup faces (QSS-M-only pics)

Approximations:

- **Pain face:** QSS-M starts it on `svc_damage`, which simple CSQC never sees,
  so the sample starts it when health drops.
- **Tab scoreboard:** draws the scorebar with kills and secrets. It isn't
  QSS-M's map/server/session-time line.
- **RGB player colours on ProQuake team slots:** ProQuake team slots are legacy
  palette indices (`0`–`13`), so a player using an RGB `0xrrggbb` pants colour
  gets no team bracket (and does not false-match team `0`).

## Portability notes

- FTE leaves `time` frozen inside `CSQC_DrawHud`. The sample times pickup
  flashes, the pain face and its autotest from `cltime`, which both engines
  update.
- `getplayerkeyvalue` with a negative player number reads the frag-sorted list
  on both QSS-M and FTEQW.
- FTE ignored the autotest's `impulse 9` and `give`, so FTE runs show a bare
  inventory. Layout-wise the HUD draws the same there.
